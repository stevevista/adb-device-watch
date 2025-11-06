// Copyright (c) 2025 R.J. (kencube@hotmail.com)
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in all
// copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.

#include "co-adb-client.h"
#include "process/process.h"
#include <asio.hpp>
#include <format>
#include <ranges>
#include <regex>
#ifdef _WIN32
#include <windows.h>
#include <io.h>
#else
#include <sys/wait.h>
#endif
#include <sys/stat.h>
#include <sys/types.h>

namespace  {

template<typename T>
inline const typename T::value_type* value_of_starts_with(
    const T& arg, 
    const typename T::value_type* name) {
    
  static_assert(std::is_same_v<typename T::value_type, char> || 
                  std::is_same_v<typename T::value_type, wchar_t>);
    
  std::basic_string_view<typename T::value_type> arg_view(arg);
  std::basic_string_view<typename T::value_type> name_view(name);
    
  if (arg_view.starts_with(name_view)) {
    return arg.data() + name_view.length();
  }
    
  return nullptr;
}

} // namespace

namespace adb_client {

using asio::awaitable;
using asio::use_awaitable;
using asio::ip::tcp;

namespace this_coro = asio::this_coro;

using namespace std::chrono_literals;

namespace {

constexpr std::string_view default_adb_server = "localhost";
constexpr std::string_view default_adb_port = "5037";

constexpr size_t MAX_PAYLOAD = 1024 * 1024;


class connection_error : public adb_error {
public:
  connection_error(const std::string& arg) noexcept : adb_error(arg) {}
};


awaitable<void>
send_protocol_string(tcp::socket &socket, std::string_view s) {
  unsigned int length = s.size();
  if (length > MAX_PAYLOAD - 4) {
    throw adb_error("message too big");
  }

  auto str = std::format("{:04x}{}", length, s);
  co_await async_write(socket, asio::buffer(str), use_awaitable);
}

awaitable<std::string>
read_protocol_string(tcp::socket &socket) {
  std::string msg;
  co_await async_read(socket,
            asio::dynamic_buffer(msg, 4), use_awaitable);

  unsigned long len = std::stoul(msg, nullptr, 16);
  msg.clear();
  co_await async_read(socket,
            asio::dynamic_buffer(msg, len), use_awaitable);

  co_return msg;
}

awaitable<void>
adb_status(tcp::socket &socket) {
  std::string msg;
  co_await async_read(socket,
            asio::dynamic_buffer(msg, 4), use_awaitable);

  if (msg == "OKAY") {
    co_return;
  }

  if (msg != "FAIL") {
    throw adb_error(std::format("protocol fault (status {:02x} {:02x} {:02x} {:02x}?!)", 
        msg[0], msg[1], msg[2], msg[3]));
  }

  msg = co_await read_protocol_string(socket);
  throw adb_error(msg);
}

awaitable<int64_t>
switch_socket_transport(tcp::socket &socket, TransportOption option) {
  int64_t transportId = 0;

  if (option.transportId) {
    co_await send_protocol_string(socket, std::format("host:transport-id:{}", *option.transportId));
  } else if (!option.serial.empty()) {
    co_await send_protocol_string(socket, std::format("host:tport:serial:{}", option.serial));
  } else if (option.transportType == TransportType::Usb) {
    co_await send_protocol_string(socket, "host:tport:usb");
  } else if (option.transportType == TransportType::Local) {
    co_await send_protocol_string(socket, "host:tport:local");
  } else {
    co_await send_protocol_string(socket, "host:tport:any");
  }

  co_await adb_status(socket);

  if (!option.transportId) {
    co_await async_read(socket,
            asio::buffer(&transportId, 8), use_awaitable);
  }

  co_return transportId;
}

enum {
  ADB_NOT_FOUND = 1,
  CREATE_PIPE_FAILED,
  SET_HANDLE_INFO_FAILED,
  CREATE_PROCESS_FAILED,
  START_ADB_SERVER_FAILED,
  SERVER_FAILED,
};

void launch_adb_server_process(std::function<void(int)> callback) {
  auto adbpath = process_lib::search_exe_path("adb"
#ifdef _WIN32
    ".exe"
#endif
    );
  if (adbpath.empty()) {
    callback(ADB_NOT_FOUND);
    return;
    // throw std::runtime_error("adb not found");
  }

  char temp[3];

#ifdef _WIN32
  SECURITY_ATTRIBUTES sa;
  sa.nLength = sizeof(sa);
  sa.lpSecurityDescriptor = NULL;
  sa.bInheritHandle = TRUE;

  HANDLE ack_read = NULL;
  HANDLE ack_write = NULL;
  if (!CreatePipe(&ack_read, &ack_write, &sa, 0)) {
    callback(CREATE_PIPE_FAILED);
    return;
    // throw std::runtime_error("CreatePipe failed");
  }

  if (!SetHandleInformation(ack_read, HANDLE_FLAG_INHERIT, 0)) {
    callback(SET_HANDLE_INFO_FAILED);
    return;
    // throw std::runtime_error("SetHandleInformation failed");
  }

  STARTUPINFOW startup;
  ZeroMemory( &startup, sizeof(startup) );
  startup.cb = sizeof(startup);
  startup.dwFlags = STARTF_USESTDHANDLES;

  const int ack_write_as_int = static_cast<int>(reinterpret_cast<INT_PTR>(ack_write));

  WCHAR   args[64];
  _snwprintf(args, 64, L"adb fork-server server --reply-fd %d",
              ack_write_as_int);

  PROCESS_INFORMATION   pinfo;
  ZeroMemory(&pinfo, sizeof(pinfo));

  if (!CreateProcessW(
            adbpath.c_str(),                              /* program path  */
            args,
                                    /* the fork-server argument will set the
                                       debug = 2 in the child           */
            NULL,                   /* process handle is not inheritable */
            NULL,                    /* thread handle is not inheritable */
            TRUE,                          /* yes, inherit some handles */
            DETACHED_PROCESS, /* the new process doesn't have a console */
            NULL,                     /* use parent's environment block */
            NULL,                    /* use parent's starting directory */
            &startup,                 /* startup info, i.e. std handles */
            &pinfo )) {
    callback(CREATE_PROCESS_FAILED);
    return;
    // throw std::runtime_error("CreateProcessW failed");
  }

  // Close handles that we no longer need to complete the rest.
  CloseHandle(pinfo.hThread);
  CloseHandle(ack_write);

  DWORD count = 0;
  // Wait for the "OK\n" message, for the pipe to be closed, or other error.
  if (!ReadFile(ack_read, temp, sizeof(temp), &count, NULL) || count != 3) {
    callback(START_ADB_SERVER_FAILED);
    return;
  }

  CloseHandle(ack_read);
  CloseHandle(pinfo.hProcess);
  // throw std::runtime_error("server not start");
#else
  int ack_fd[2];
  if (::pipe(ack_fd) != 0) {
    callback(CREATE_PIPE_FAILED);
    return;
  }

  pid_t pid = fork();
  if (pid < 0) {
    callback(CREATE_PROCESS_FAILED);
    return;
  }

  if (pid == 0) {
    // child side of the fork
    close(ack_fd[0]);

    fcntl(ack_fd[1], F_SETFD, 0);

    char reply_fd[30];
    snprintf(reply_fd, sizeof(reply_fd), "%d", ack_fd[1]);
        // child process
    execl(adbpath.c_str(), "adb", "fork-server", "server",
                           "--reply-fd", reply_fd, NULL);
  } else {
    // parent side of the fork
    // wait for the "OK\n" message
    close(ack_fd[1]);
    int ret = read(ack_fd[0], temp, 3);
    close(ack_fd[0]);
    if (ret != 3) {
      callback(START_ADB_SERVER_FAILED);
      return;
    }
  }
#endif

  callback(memcmp(temp, "OK\n", 3) == 0 ? 0 : SERVER_FAILED);
}

template<typename ResponseHandler = asio::use_awaitable_t<>>
auto launch_server(ResponseHandler&& handler = {}) {
  auto initiate = []<typename Handler>(Handler&& self) mutable
    {
      std::thread([self = std::make_shared<Handler>(std::forward<Handler>(self))]() mutable {
        launch_adb_server_process([self](int r)
        {
            (*self)(r);
        });
      }).detach();
    };

  return asio::async_initiate<
        asio::use_awaitable_t<>, void(int)>(
            initiate, handler
        );
}

awaitable<tcp::socket>
connect(
    tcp::endpoint target,
    std::string_view service,
    TransportOption option,
    int64_t *transportId) {
  static bool serverLaunchTried = false;

  auto ex = co_await this_coro::executor;
  tcp::socket client(ex);

  for (;;) {
    try {
      co_await client.async_connect(target, use_awaitable);
      break;
    } catch (std::exception &e) {
      if (!option.launchServerIfNeed || serverLaunchTried)
        throw connection_error(e.what());
    }

    serverLaunchTried = true;

    int r = co_await launch_server();
    if (r != 0) {
      throw connection_error("start adb server failed");
    }

    // loop & try connect again
  }

  if (!service.starts_with("host")) {
    auto id = co_await switch_socket_transport(client, option);
    if (transportId) {
      *transportId = id;
    }
  }

  co_await send_protocol_string(client, service);

  co_await adb_status(client);

  co_return client;
}

std::string
format_host_command(
    std::string_view command,
    TransportOption option) {
  if (option.transportId) {
    return std::format("host-transport-id:{}:{}", *option.transportId, command);
  }

  if (!option.serial.empty()) {
    return std::format("host-serial:{}:{}", option.serial, command);
  }

  if (option.transportType == TransportType::Usb) {
    return std::format("host-usb:{}", command);
  }

  if (option.transportType == TransportType::Local) {
    return std::format("host-local:{}", command);
  }

  return std::format("host:{}", command);
}

awaitable<std::tuple<uint8_t, std::vector<char>, std::vector<char>>>
read_shell_output(tcp::socket &socket) {
  enum Id : uint8_t {
    kIdStdin = 0,
    kIdStdout = 1,
    kIdStderr = 2,
    kIdExit = 3,

    // Close subprocess stdin if possible.
    kIdCloseStdin = 4,

    // Window size change (an ASCII version of struct winsize).
    kIdWindowSizeChange = 5,

    // Indicates an invalid or unknown packet.
    kIdInvalid = 255,
  };

  constexpr auto kBufferSize = 40960;
  constexpr auto kHeaderSize = sizeof(Id) + sizeof(uint32_t);

  std::vector<char> output;
  std::vector<char> errout;
  uint8_t exitCode = 0;

  size_t bytes_left = 0;
  char buffer[kBufferSize];
  char* buffer_end = buffer + sizeof(buffer);

  auto data = [&]() -> char* { return buffer + kHeaderSize; }; 
  auto data_capacity = [&]() -> size_t { return buffer_end - data(); };

  for (;;) {
    // Only read a new header if we've finished the last packet.
    if (!bytes_left) {
      co_await async_read(socket,
              asio::buffer(buffer, kHeaderSize), use_awaitable);

      uint32_t packet_length;
      memcpy(&packet_length, &buffer[1], sizeof(packet_length));

      bytes_left = packet_length;
    }

    size_t data_length = std::min(bytes_left, data_capacity());
    if (data_length) {
      co_await async_read(socket,
              asio::buffer(data(), data_length), use_awaitable);
      bytes_left -= data_length;
    }

    bool done = false;

    switch (buffer[0]) {
      case kIdStdout:
        output.insert(output.end(), data(), data() + data_length);
        break;
      case kIdStderr:
        errout.insert(errout.end(), data(), data() + data_length);
        break;
      case kIdExit:
        done = true;
        exitCode = static_cast<uint8_t>(data()[0]);
        break;
    }

    if (done) {
      break;
    }
  }

  co_return std::make_tuple(exitCode, std::move(output), std::move(errout));
}

awaitable<std::tuple<uint8_t, std::vector<char>, std::vector<char>>>
read_output(tcp::socket &socket) {
  std::vector<char> output;
  std::vector<char> errout;

  constexpr auto kBufferSize = 40960;
  char buffer[kBufferSize];

  for (;;) {
    try {
      std::size_t n = co_await socket.async_read_some(asio::buffer(buffer), use_awaitable);
      output.insert(output.end(), buffer, buffer + n);
    } catch (asio::system_error& e) {
      if (e.code() == asio::error::eof) {
        break;
      }
      throw;
    }
  }

  co_return std::make_tuple((uint8_t)0, std::move(output), std::move(errout));
}

awaitable<tcp::endpoint>
resolve_endpoint(TransportOption option) {
  auto ex = co_await this_coro::executor;
  tcp::socket client(ex);

  auto resolver = use_awaitable.as_default_on(tcp::resolver(ex));
  tcp::endpoint target = *(co_await resolver.async_resolve(tcp::v4(), 
            option.server.empty() ? default_adb_server : option.server,
            option.port.empty() ? default_adb_port : option.port)).begin();

  co_return target;
}

awaitable<std::string>
co_query(
    tcp::endpoint target,
    std::string_view service,
    TransportOption option) {
  try {
    auto client = co_await connect(
          target,
          service,
          option,
          nullptr);
    co_return co_await read_protocol_string(client);
  } catch (connection_error &) {
    if (!option.launchServerIfNeed) {
      // if cause is adb server not aviable
      // and we chouse not to start it
      // then we do not treat this as an error
      // simply return empty string
    }
  }

  co_return "";
}

awaitable<std::string>
co_command_query(
    tcp::endpoint target,
    std::string_view command,
    TransportOption option) {
  try {
    auto client = co_await connect(
          target,
          format_host_command(command, option),
          {},
          nullptr);
    co_return co_await read_protocol_string(client);
  } catch (connection_error &) {
    if (!option.launchServerIfNeed) {
      // if cause is adb server not aviable
      // and we chouse not to start it
      // then we do not treat this as an error
      // simply return empty string
    }
  }

  co_return "";
}

awaitable<void>
co_command(
    tcp::endpoint target,
    std::string_view command,
    TransportOption option,
    std::optional<std::chrono::milliseconds> timeout) {
  auto client = co_await connect(
        target,
        format_host_command(command, option),
        {},
        nullptr);

  if (timeout) {
    std::condition_variable cond;
    std::mutex mt;
    std::unique_lock lk(mt);
    bool executed = false;

    auto th = std::thread([&]() {
      cond.wait_for(lk, *timeout, [&executed] {
        return executed;
      });
      if (!executed) {
        client.close();
      }
    });

    try {
      co_await adb_status(client);
      executed = true;
      cond.notify_one();
      th.join();
    } catch (std::exception &) {
      th.join();
      throw adb_error("command timeout");
    }
  } else {
    co_await adb_status(client);
  }
}

awaitable<std::vector<char>>
co_command_connect(
    tcp::endpoint target,
    std::string_view command,
    TransportOption option,
    int64_t *transportId) {
  auto client = co_await connect(
        target,
        command,
        option,
        transportId);

  std::vector<char> output;

  constexpr auto kBufferSize = 40960;
  char buffer[kBufferSize];

  for (;;) {
    try {
      std::size_t n = co_await client.async_read_some(asio::buffer(buffer), use_awaitable);
      output.insert(output.end(), buffer, buffer + n);
    } catch (asio::system_error& e) {
      if (e.code() == asio::error::eof) {
        break;
      }
      throw;
    }
  }

  co_return output;
}

awaitable<std::vector<std::string>>
co_get_features(
    tcp::endpoint target,
    TransportOption option) {
  auto feature_str = co_await co_command_query(
      target,
      "features",
      option);

  auto v = feature_str
           | std::views::split(',')
           | std::views::transform([](auto word) {
               return std::string(word.begin(), word.end());
           });

  co_return std::vector<std::string>(v.begin(), v.end());
}

awaitable<void>
co_wait_device(
    tcp::endpoint target,
    std::string_view state,
    TransportOption option,
    std::optional<std::chrono::milliseconds> timeout) {
  std::string_view targetType;
  if (option.transportType == TransportType::Usb) {
    targetType = "usb";
  } else if (option.transportType == TransportType::Local) {
    targetType = "local";
  } else {
    targetType = "any";
  }

  co_await co_command(
    target,
    std::format("wait-for-{}-{}", targetType, state),
    option,
    timeout);
}

awaitable<std::tuple<uint8_t, std::vector<char>, std::vector<char>>>
co_execute_shell(
    tcp::endpoint target,
    std::string_view command,
    TransportOption option,
    std::optional<bool> use_shell_protocol) {
  bool shell_protocol;
  if (use_shell_protocol.has_value()) {
    shell_protocol = *use_shell_protocol;
  } else {
    auto features = co_await co_get_features(target, option);
    shell_protocol = std::ranges::find(features, "shell_v2") != features.end();
  }

  auto client = co_await connect(
        target,
        std::format("shell{}:{}", shell_protocol ? ",v2,raw" : "", command),
        option,
        nullptr);

  if (shell_protocol) {
    co_return co_await read_shell_output(client);
  } else {
    co_return co_await read_output(client);
  }
}

} // namespace

awaitable<void>
co_wait_device(
    std::string_view state,
    TransportOption option,
    std::optional<std::chrono::milliseconds> timeout) {
  co_await co_wait_device(
    co_await resolve_endpoint(option),
    state,
    option,
    timeout);
}

awaitable<void>
co_kill(TransportOption option) noexcept {
  try {
    auto ex = co_await this_coro::executor;
    tcp::socket client(ex);

    co_await client.async_connect(co_await resolve_endpoint(option), use_awaitable);

    co_await send_protocol_string(client, "host:kill");

    // The server might send OKAY, so consume that.
    std::string msg;
    co_await async_read(client,
                asio::dynamic_buffer(msg, 4), use_awaitable);
  }  catch (std::exception &) {
    // do not treat as error
    co_return;
  }
}

awaitable<std::string>
co_query(
    std::string_view service,
    TransportOption option) {
  co_return co_await co_query(
    co_await resolve_endpoint(option),
    service,
    option);
}

awaitable<std::vector<DeviceInfo>>
co_list_devices(TransportOption option, bool device_only, std::string_view target_serial) {
  auto liststr = co_await co_query(
      "host:devices-l",
      option);

  auto v = liststr
           | std::views::split('\n')
           | std::views::transform([](auto word) {
               return std::string_view(word.begin(), word.end());
             });

  auto lines = std::vector<std::string_view>(v.begin(), v.end());

  std::regex ws_re("\\s+");

  std::vector<DeviceInfo> out;

  for (auto line : lines) {
    if (!line.empty()) {
      std::regex_token_iterator<std::string_view::const_iterator> first {line.begin(), line.end(), ws_re, -1}, last;
      auto items = std::vector<std::string>(first, last);
      if (items.size() >= 2) {
        DeviceInfo dev;
        dev.serial = std::move(items[0]);
        dev.state = std::move(items[1]);

        if (device_only && dev.state != "device") {
          continue;
        }

        if (!target_serial.empty() && target_serial != dev.serial) {
          continue;
        }
  
        for (size_t i = 2; i < items.size(); i++) {
          if (auto *val = value_of_starts_with(items[i], "product:")) {
            dev.product = val;
          } else if (auto *val = value_of_starts_with(items[i], "model:")) {
            dev.model = val;
          } else if (auto *val = value_of_starts_with(items[i], "device:")) {
            dev.device = val;
          } else if (auto *val = value_of_starts_with(items[i], "transport_id:")) {
            dev.transportId = strtoul(val, nullptr, 10);
          }
        }

        out.push_back(std::move(dev));
      }
    }
  }

  co_return out;
}

awaitable<void>
co_command(
    std::string_view command,
    TransportOption option,
    std::optional<std::chrono::milliseconds> timeout) {
  co_await co_command(
    co_await resolve_endpoint(option),
    command,
    option,
    timeout);
}

awaitable<std::string>
co_command_query(
    std::string_view command,
    TransportOption option) {
  co_return co_await co_command_query(
    co_await resolve_endpoint(option),
    command,
    option);
}

awaitable<std::vector<char>>
co_command_connect(
    std::string_view command,
    TransportOption option) {
  co_return co_await co_command_connect(
      co_await resolve_endpoint(option),
      command,
      option,
      nullptr);
}

awaitable<std::vector<std::string>>
co_get_features(
    TransportOption option) {
  co_return co_await co_get_features(
      co_await resolve_endpoint(option),
      option);
}

awaitable<std::tuple<uint8_t, std::vector<char>, std::vector<char>>>
co_execute_shell(
    std::string_view command,
    TransportOption option,
    std::optional<bool> use_shell_protocol) {
  co_return co_await co_execute_shell(
    co_await resolve_endpoint(option),
    command,
    option,
    use_shell_protocol);
}

awaitable<void>
co_remount(
    TransportOption option,
    std::optional<bool> use_remount_shell,
    std::string_view args) {
  auto target = co_await resolve_endpoint(option);

  bool remount_shell;
  bool shell_protocol = false;
  if (use_remount_shell.has_value()) {
    remount_shell = *use_remount_shell;
  } else {
    auto features = co_await co_get_features(target, option);
    if (std::ranges::find(features, "remount_shell") != features.end()) {
      remount_shell = true;
      shell_protocol = std::ranges::find(features, "shell_v2") != features.end();
    }
  }

  if (remount_shell) {
    auto client = co_await connect(
        target,
        std::format("shell{}:remount {}", shell_protocol ? ",v2,raw" : "", args),
        option,
        nullptr);

    if (shell_protocol) {
      co_await read_shell_output(client);
    } else {
      co_await read_output(client);
    }
  } else {
    co_await co_command_connect(
        target,
        std::format("remount:{}", args),
        option,
        nullptr);
  }
}

awaitable<void>
co_root(bool root, TransportOption option) {
  auto target = co_await resolve_endpoint(option);

  int64_t transportId;
  auto client = co_await connect(
        target,
        root ? "root:" : "unroot:",
        option,
        &transportId);

  // Figure out whether we actually did anything.
  char buffer[256] = {0};
  co_await client.async_read_some(asio::buffer(buffer), use_awaitable);

  // adbd is already running as root
  if (strstr(buffer, "already running as root")) {
    co_return;
  }

  //wait_for_device("wait-for-disconnect");
  co_await co_wait_device(
      target,
      "disconnect",
      { .transportId = transportId, },
      std::nullopt);

  // Wait for the device to come back.
  // If we were using a specific transport ID, there's nothing we can wait for.
  if (!option.transportId) {
    co_await co_wait_device(
        target,
        "device",
        option,
        6000ms);
  }
}

// sync

namespace {

constexpr size_t SYNC_DATA_MAX = 64 * 1024;

using LocalPath = std::filesystem::path;

// dirname("//foo") returns "//", so we can't do the obvious `path == "/"`.
bool is_root_dir(std::string_view path) {
  for (char c : path) {
    if (c != '/') {
      return false;
    }
  }
  return true;
}

std::string posix_dirname(std::string_view path) {
  if (!path.empty() && path.back() == '/') {
    path = path.substr(0, path.size() - 1);
  }

  auto pos = path.rfind('/');
  if (pos != path.npos) {
    path = path.substr(0, pos + 1);
  }

  if (path.empty()) {
    return "/";
  }

  return std::string(path);
}


std::string posix_basename(std::string_view path) {
  auto pos = path.rfind('/');
  return pos == path.npos ? std::string(path) : std::string(path.substr(pos + 1));
}

std::string posix_join(std::string path, const std::string &name) {
  if (path.back() != '/') {
    path.push_back('/');
  }
  path.append(name);
  return path;
}

std::string escape_arg(const std::string& s) {
  // Escape any ' in the string (before we single-quote the whole thing).
  // The correct way to do this for the shell is to replace ' with '\'' --- that is,
  // close the existing single-quoted string, escape a single single-quote, and start
  // a new single-quoted string. Like the C preprocessor, the shell will concatenate
  // these pieces into one string.

  std::string result;
  result.push_back('\'');

  size_t base = 0;
  while (true) {
    size_t found = s.find('\'', base);
    result.append(s, base, found - base);
    if (found == s.npos) break;
    result.append("'\\''");
    base = found + 1;
  }

  result.push_back('\'');
  return result;
}

#ifdef _WIN32

#define S_IFLNK 0120000
#define S_IXUSR 00100
#define S_IXGRP 00010
#define S_IXOTH 00001
#define S_ISLNK(mode) (((mode) & S_IFMT) == S_IFLNK)
#define S_ISDIR(mode) (((mode) & _S_IFDIR) == _S_IFDIR)
#define S_ISREG(mode) (((mode) & S_IFMT) == S_IFREG)
#define S_ISEXE(mode) (!!((mode) & (S_IXUSR | S_IXGRP | S_IXOTH)))

#define lstat stat

#endif

#define MKID(a, b, c, d) ((a) | ((b) << 8) | ((c) << 16) | ((d) << 24))

#define ID_LSTAT_V1 MKID('S', 'T', 'A', 'T')
#define ID_STAT_V2 MKID('S', 'T', 'A', '2')
#define ID_LSTAT_V2 MKID('L', 'S', 'T', '2')

#define ID_LIST_V1 MKID('L', 'I', 'S', 'T')
#define ID_LIST_V2 MKID('L', 'I', 'S', '2')
#define ID_DENT_V1 MKID('D', 'E', 'N', 'T')
#define ID_DENT_V2 MKID('D', 'N', 'T', '2')

#define ID_SEND_V1 MKID('S', 'E', 'N', 'D')
#define ID_SEND_V2 MKID('S', 'N', 'D', '2')
#define ID_RECV_V1 MKID('R', 'E', 'C', 'V')
#define ID_RECV_V2 MKID('R', 'C', 'V', '2')
#define ID_DONE MKID('D', 'O', 'N', 'E')
#define ID_DATA MKID('D', 'A', 'T', 'A')
#define ID_OKAY MKID('O', 'K', 'A', 'Y')
#define ID_FAIL MKID('F', 'A', 'I', 'L')
#define ID_QUIT MKID('Q', 'U', 'I', 'T')


struct sync_stat_v1 {
  uint32_t id;
  uint32_t mode;
  uint32_t size;
  uint32_t mtime;
};

struct sync_dent_v1 {
  uint32_t id;
  uint32_t mode;
  uint32_t size;
  uint32_t mtime;
};  // followed by `namelen` bytes of the name.

struct sync_dent_v2 {
  uint32_t id;
  uint32_t error;
  uint64_t dev;
  uint64_t ino;
  uint32_t mode;
  uint32_t nlink;
  uint32_t uid;
  uint32_t gid;
  uint64_t size;
  int64_t atime;
  int64_t mtime;
  int64_t ctime;
};  // followed by `namelen` bytes of the name.

struct sync_status {
  uint32_t id;
  union {
    uint32_t error;
    uint32_t length;
    uint32_t mtime;
  } u;
};  // followed by `size` bytes of data.

awaitable<void>
sync_send_request(tcp::socket &socket, uint32_t id, std::string_view path) {
  uint32_t length = path.length();

  if (length > 1024) {
    throw adb_sync_error("sync path length too long", -1);
  }

  std::vector<char> buf(8 + length);
  memcpy(&buf[0], &id, 4);
  memcpy(&buf[4], &length, 4);
  memcpy(&buf[8], path.data(), length);
  co_await async_write(socket, asio::buffer(buf), use_awaitable);
}

awaitable<Stat>
sync_finish_stat(tcp::socket &socket, bool have_stat_v2) {
  Stat st;

  if (have_stat_v2) {
    sync_status hdr;
    co_await async_read(socket,
            asio::buffer(&hdr, sizeof(hdr)), use_awaitable);
    co_await async_read(socket,
            asio::buffer(&st, sizeof(st)), use_awaitable);

    if (hdr.id != ID_LSTAT_V2 && hdr.id != ID_STAT_V2) {
      throw adb_sync_error("protocol fault: stat response has wrong message id " + std::to_string(hdr.id), -1);
    }

    if (hdr.u.error != 0) {
      throw adb_sync_error("protocol fault: sync finish error", hdr.u.error);
    }
  } else {
    sync_stat_v1 stat_v1;
  
    co_await async_read(socket,
            asio::buffer(&stat_v1, sizeof(stat_v1)), use_awaitable);

    if (stat_v1.id != ID_LSTAT_V1) {
      throw adb_sync_error("protocol fault: stat response has wrong message id " + std::to_string(stat_v1.id), -1);
    }

    st.mode = stat_v1.mode;
    st.size = stat_v1.size;
    st.ctime = stat_v1.mtime;
    st.mtime = stat_v1.mtime;
  }

  co_return st;
}

awaitable<Stat>
sync_lstat(tcp::socket &socket, std::string_view path, bool have_stat_v2) {
  co_await sync_send_request(socket, have_stat_v2 ? ID_LSTAT_V2 : ID_LSTAT_V1, path);
  co_return co_await sync_finish_stat(socket, have_stat_v2);
}

awaitable<Stat>
sync_stat(tcp::socket &socket, std::string_view path, bool have_stat_v2) {
  co_await sync_send_request(socket, have_stat_v2 ? ID_STAT_V2 : ID_LSTAT_V1, path);
  auto st = co_await sync_finish_stat(socket, have_stat_v2);

  if (!have_stat_v2 && S_ISLNK(st.mode)) {
    // If the target is a symlink, figure out whether it's a file or a directory.
    // Also, zero out the st_size field, since no one actually cares what the path length is.
    st.size = 0;
    st.mode &= ~S_IFMT;
    try {
      co_await sync_lstat(socket, std::format("{}/", path), have_stat_v2);
      st.mode |= S_IFDIR;
    } catch (std::exception &) {
      st.mode |= S_IFREG;
    }
  }
  co_return st;
}

template <bool v2>
awaitable<std::vector<ListItem>>
sync_finish_ls(tcp::socket &socket) {
  using dent_type =
                std::conditional_t<v2, sync_dent_v2, sync_dent_v1>;

  std::vector<ListItem> out;

  for (;;) {
    ListItem item;
    dent_type dent;
    uint32_t namelen;
    co_await async_read(socket, asio::buffer(&dent, sizeof(dent)), use_awaitable);
    co_await async_read(socket, asio::buffer(&namelen, sizeof(namelen)), use_awaitable);
    
    if (dent.id == ID_DONE) {
      break;
    }
    
    uint32_t expected_id = v2 ? ID_DENT_V2 : ID_DENT_V1;
    if (dent.id != expected_id) {
      throw adb_sync_error("unexpected dent id", -1);
    }

    char buf[256];
    size_t len = namelen;
    if (len > 255) {
      throw adb_sync_error("dent namelen too long", -1);
    }

    co_await async_read(socket, asio::buffer(buf, len), use_awaitable);
    buf[len] = 0;

    item.name = buf;
    item.mode = dent.mode;
    item.size = dent.size;
    item.mtime = dent.mtime;

    out.push_back(std::move(item));
  }

  co_return out;
}

awaitable<std::vector<ListItem>>
sync_list(tcp::socket &socket, std::string_view path, bool has_ls_v2) {
  co_await sync_send_request(socket, has_ls_v2 ? ID_LIST_V2 : ID_LIST_V1, path);
  if (has_ls_v2) {
    co_return co_await sync_finish_ls<true>(socket);
  } else {
    co_return co_await sync_finish_ls<false>(socket);
  }
}

struct copyinfo {
  LocalPath lpath;
  std::string rpath;
  int64_t time = 0;
  uint32_t mode;
  uint64_t size = 0;

  copyinfo(const LocalPath& local_path,
             const std::string& remote_path,
             const std::string& name,
             unsigned int mode)
            : lpath(local_path / name), rpath(posix_join(remote_path, name)), mode(mode) {
    if (S_ISDIR(mode)) {
      if (rpath.back() != '/') {
        rpath.push_back('/');
      }
    }
  }
};

awaitable<void>
sync_recv(
    tcp::socket &socket,
    std::string_view rpath,
    const LocalPath &lpath) {
  co_await sync_send_request(socket, ID_RECV_V1, rpath);

  asio::basic_stream_file lfile(co_await this_coro::executor, lpath.string(), asio::file_base::flags::create | asio::file_base::flags::truncate | asio::file_base::flags::write_only);
  
  try {
    for (;;) {
      sync_status data;
      co_await async_read(socket, asio::buffer(&data, sizeof(data)), use_awaitable);

      if (data.id == ID_DONE) break;

      if (data.id != ID_DATA) {
        throw adb_sync_error("bad sync recv id", -1);
      }

      if (data.u.length > SYNC_DATA_MAX) {
        throw adb_sync_error("sync recv size too large", -1);
      }

      std::vector<char> buffer;
      co_await async_read(socket, asio::dynamic_buffer(buffer, data.u.length), use_awaitable);
      co_await async_write(lfile, asio::buffer(buffer), use_awaitable);
    }
  } catch (std::exception &) {
    std::error_code ec;
    std::filesystem::remove(lpath, ec);
    throw;
  }
}

awaitable<std::vector<char>>
sync_recv_buffer(
    tcp::socket &socket,
    std::string_view rpath) {
  co_await sync_send_request(socket, ID_RECV_V1, rpath);

  std::vector<char> out_buffer;

  for (;;) {
    sync_status data;
    co_await async_read(socket, asio::buffer(&data, sizeof(data)), use_awaitable);

    if (data.id == ID_DONE) break;

    if (data.id != ID_DATA) {
      throw adb_sync_error("bad sync recv id", -1);
    }

    if (data.u.length > SYNC_DATA_MAX) {
      throw adb_sync_error("sync recv size too large", -1);
    }

    auto last_pos = out_buffer.size();
    out_buffer.resize(last_pos + data.u.length);
    co_await async_read(socket, asio::buffer(&out_buffer[last_pos], data.u.length), use_awaitable);
  }

  co_return out_buffer;
}

struct syncsendbuf {
    unsigned id;
    unsigned size;
    char data[SYNC_DATA_MAX];
};

awaitable<void>
sync_send_buffer(
    tcp::socket &socket,
    std::string_view rpath,
    const char *buffer,
    size_t size) {
  uint32_t mode = 0777;
  unsigned mtime = 0;

  auto path_and_mode = std::format("{},{}", rpath, mode);
  if (path_and_mode.length() > 1024) {
    throw adb_sync_error("SendFile failed: path too long", -1);
  }

  if (size < SYNC_DATA_MAX) {
    std::vector<char> buf(sizeof(sync_status) + path_and_mode.length() + sizeof(sync_status) +
                              size + sizeof(sync_status));
    char* p = &buf[0];

    auto* req_send = reinterpret_cast<sync_status*>(p);
    req_send->id = ID_SEND_V1;
    req_send->u.length = path_and_mode.length();
    p += sizeof(sync_status);
    memcpy(p, path_and_mode.data(), path_and_mode.size());
    p += path_and_mode.length();

    auto* req_data = reinterpret_cast<sync_status*>(p);
    req_data->id = ID_DATA;
    req_data->u.length = size;
    p += sizeof(sync_status);
    memcpy(p, buffer, size);
    p += size;

    auto* req_done = reinterpret_cast<sync_status*>(p);
    req_done->id = ID_DONE;
    req_done->u.mtime = mtime;
    p += sizeof(sync_status);

    co_await async_write(socket, asio::buffer(buf), use_awaitable);
  } else {
    co_await sync_send_request(socket, ID_SEND_V1, path_and_mode);

    size_t n = SYNC_DATA_MAX;
    for (; n > 0;) {
      syncsendbuf sbuf;
      sbuf.id = ID_DATA;
      sbuf.size = n;
      memcpy(sbuf.data, buffer, n);
      co_await async_write(socket, asio::buffer(&sbuf, sizeof(sync_status) + n), use_awaitable);

      size -= n;
      buffer += n;
      n = size < SYNC_DATA_MAX ? size : SYNC_DATA_MAX;
    }

    sync_status data;
    data.id = ID_DONE;
    data.u.mtime = mtime;
    co_await async_write(socket, asio::buffer(&data, sizeof(data)), use_awaitable);
  }

  sync_status data;
  co_await async_read(socket, asio::buffer(&data, sizeof(data)), use_awaitable);

  if (data.id == ID_OKAY) {
    if (data.u.length != 0) {
      throw adb_sync_error(std::format("received ID_OKAY with msg_len {} != 0", data.u.length), -1);
    }
    co_return;
  } else if (data.id != ID_FAIL) {
    throw adb_sync_error(std::format("unexpected response from daemon: id = {}", data.id), -1);
  } else if (data.u.length > SYNC_DATA_MAX) {
    throw adb_sync_error(std::format("too-long message length from daemon: msglen = {}", data.u.length), -1);
  }

  std::string errmsg;
  co_await async_read(socket, asio::dynamic_buffer(errmsg, data.u.length), use_awaitable);

  throw adb_sync_error(errmsg, data.id);
}

awaitable<void>
sync_send(
    tcp::socket &socket,
    std::string_view rpath,
    const LocalPath &lpath,
    uint32_t mode,
    unsigned mtime) {
  asio::basic_stream_file lfile(co_await this_coro::executor, lpath.string(), asio::file_base::flags::read_only);
  
  auto path_and_mode = std::format("{},{}", rpath, mode);
  if (path_and_mode.length() > 1024) {
    throw adb_sync_error("SendFile failed: path too long", -1);
  }

  syncsendbuf sbuf;
  sbuf.id = ID_DATA;

  auto n = co_await lfile.async_read_some(asio::buffer(sbuf.data), use_awaitable);
  if (n < SYNC_DATA_MAX) {
    std::vector<char> buf(sizeof(sync_status) + path_and_mode.length() + sizeof(sync_status) +
                              n + sizeof(sync_status));
    char* p = &buf[0];

    auto* req_send = reinterpret_cast<sync_status*>(p);
    req_send->id = ID_SEND_V1;
    req_send->u.length = path_and_mode.length();
    p += sizeof(sync_status);
    memcpy(p, path_and_mode.data(), path_and_mode.size());
    p += path_and_mode.length();

    auto* req_data = reinterpret_cast<sync_status*>(p);
    req_data->id = ID_DATA;
    req_data->u.length = n;
    p += sizeof(sync_status);
    memcpy(p, sbuf.data, n);
    p += n;

    auto* req_done = reinterpret_cast<sync_status*>(p);
    req_done->id = ID_DONE;
    req_done->u.mtime = mtime;
    p += sizeof(sync_status);

    co_await async_write(socket, asio::buffer(buf), use_awaitable);
  } else {
    co_await sync_send_request(socket, ID_SEND_V1, path_and_mode);

    for (;;) {
      sbuf.size = n;
      co_await async_write(socket, asio::buffer(&sbuf, sizeof(sync_status) + n), use_awaitable);

      try {
        n = co_await lfile.async_read_some(asio::buffer(sbuf.data), use_awaitable);
        if (n == 0) {
          break;
        }
      } catch (asio::system_error &e) {
        if (e.code() == asio::error::eof) {
          break;
        }
        throw;
      }
    }

    sync_status data;
    data.id = ID_DONE;
    data.u.mtime = mtime;
    co_await async_write(socket, asio::buffer(&data, sizeof(data)), use_awaitable);
  }

  sync_status data;
  co_await async_read(socket, asio::buffer(&data, sizeof(data)), use_awaitable);

  if (data.id == ID_OKAY) {
    if (data.u.length != 0) {
      throw adb_sync_error(std::format("received ID_OKAY with msg_len {} != 0", data.u.length), -1);
    }
    co_return;
  } else if (data.id != ID_FAIL) {
    throw adb_sync_error(std::format("unexpected response from daemon: id = {}", data.id), -1);
  } else if (data.u.length > SYNC_DATA_MAX) {
    throw adb_sync_error(std::format("too-long message length from daemon: msglen = {}", data.u.length), -1);
  }

  std::string errmsg;
  co_await async_read(socket, asio::dynamic_buffer(errmsg, data.u.length), use_awaitable);

  throw adb_sync_error(errmsg, data.id);
}


awaitable<std::vector<copyinfo>>
remote_build_list(
    tcp::socket &socket,
    std::string_view rpath,
    const LocalPath &lpath,
    bool have_stat_v2,
    bool has_ls_v2) {
  std::vector<copyinfo> file_list;
  std::vector<copyinfo> dirlist;

  // Add an entry for the current directory to ensure it gets created before pulling its contents.
  file_list.push_back(copyinfo {
    lpath.parent_path(),
    posix_dirname(rpath),
    posix_basename(rpath),
    S_IFDIR
  });

  auto items = co_await sync_list(socket, rpath, has_ls_v2);
  for (auto &item : items) {
    if (item.name == "." || item.name == "..") {
      continue;
    }
  
    copyinfo ci(lpath, std::string(rpath), item.name, item.mode);
    if (S_ISDIR(item.mode)) {
      dirlist.push_back(ci);
    } else if (S_ISLNK(item.mode)) {
      // Check each symlink we found to see whether it's a file or directory.
      try {
        auto st = co_await sync_stat(socket, ci.rpath, have_stat_v2);
        if (S_ISDIR(st.mode)) {
          dirlist.emplace_back(std::move(ci));
        } else {
          file_list.emplace_back(std::move(ci));
        }
      } catch (std::exception &) {
        // ignore
      }
    } else if (S_ISREG(item.mode)) {
      ci.time = item.mtime;
      ci.size = item.size;
      file_list.push_back(ci);
    }
  }

  // Recurse into each directory we found.
  while (!dirlist.empty()) {
    copyinfo current = dirlist.back();
    dirlist.pop_back();
    
    auto sublist = co_await remote_build_list(
        socket,
        current.rpath,
        current.lpath,
        have_stat_v2,
        has_ls_v2);
    file_list.insert(file_list.end(), sublist.begin(), sublist.end());
  }

  co_return file_list;
}

awaitable<void>
copy_remote_dir_local(
    tcp::socket &socket,
    std::string rpath,
    const LocalPath &lpath,
    bool have_stat_v2,
    bool has_ls_v2) {
  // Make sure that both directory paths end in a slash.
  // Both paths are known to be nonempty, so we don't need to check.
  if (rpath.back() != '/') {
    rpath.push_back('/');
  }

  auto file_list = co_await remote_build_list(
        socket,
        rpath,
        lpath,
        have_stat_v2,
        has_ls_v2);

  for (const copyinfo &ci : file_list) {
    if (S_ISDIR(ci.mode)) {
      // Entry is for an empty directory, create it and continue.
      // TODO(b/25457350): We don't preserve permissions on directories.
      std::error_code ec;
      if (!std::filesystem::exists(ci.lpath, ec) && !std::filesystem::create_directories(ci.lpath, ec))  {
        throw adb_sync_error(std::format("failed to create directory '{}'", ci.lpath.string()), -1);
      }
      continue;
    }

    co_await sync_recv(socket, ci.rpath, ci.lpath);
  }
}

void
local_build_list(
    std::vector<copyinfo> &file_list,
    std::vector<std::string> &directory_list,
    const LocalPath& lpath,
    std::string_view rpath) {
  std::vector<copyinfo> dirlist;

  for (auto const& dir_entry : std::filesystem::directory_iterator{lpath}) {
    // std::cout << dir_entry.path() << std::endl;
    struct stat st;
    if (lstat(dir_entry.path().string().c_str(), &st) == -1) {
      continue;
    }

    copyinfo ci(lpath, std::string(rpath), dir_entry.path().filename().string(), st.st_mode);
    if (S_ISDIR(st.st_mode)) {
      dirlist.push_back(std::move(ci));
    } else if (S_ISREG(st.st_mode)) {
      ci.time = st.st_mtime;
      ci.size = st.st_size;
      file_list.push_back(std::move(ci));
    }
  }

  for (const copyinfo& ci : dirlist) {
    directory_list.push_back(ci.rpath);
    local_build_list(file_list, directory_list, ci.lpath, ci.rpath);
  }
}

awaitable<void>
copy_local_dir_remote(
    tcp::socket &socket,
    const LocalPath &lpath,
    std::string rpath,
    bool have_fixed_push_mkdir,
    bool have_shell_v2,
    tcp::endpoint &target,
    TransportOption option) {
  // Make sure that both directory paths end in a slash.
  // Both paths are known to be nonempty, so we don't need to check.
  if (rpath.back() != '/') {
    rpath.push_back('/');
  }

  // Recursively build the list of files to copy.
  std::vector<copyinfo> file_list;
  std::vector<std::string> directory_list;

  for (auto path = rpath; !is_root_dir(path); path = posix_dirname(path)) {
    directory_list.push_back(path);
  }
  std::reverse(directory_list.begin(), directory_list.end());

  local_build_list(file_list, directory_list, lpath, rpath);

  // b/110953234:
  // P shipped with a bug that causes directory creation as a side-effect of a push to fail.
  // Work around this by explicitly doing a mkdir via shell.
  //
  // Devices that don't support shell_v2 are unhappy if we try to send a too-long packet to them,
  // but they're not affected by this bug, so only apply the workaround if we have shell_v2.
  //
  // TODO(b/25457350): We don't preserve permissions on directories.
  // TODO: Find all of the leaves and `mkdir -p` them instead?
  if (!have_fixed_push_mkdir &&
    have_shell_v2) {
    std::string cmd = "mkdir";
    for (const auto& dir : directory_list) {
      std::string escaped_path = escape_arg(dir);
      if (escaped_path.size() > 16384) {
        // Somewhat arbitrarily limit that probably won't ever happen.
        throw adb_sync_error(std::format("path too long: {}", escaped_path), -1);
      }

      // The maximum should be 64kiB, but that's not including other stuff that gets tacked
      // onto the command line, so let's be a bit conservative.
      if (cmd.size() + escaped_path.size() > 32768) {
        // Dispatch the command, ignoring failure (since the directory might already exist).
        try {
          co_await co_execute_shell(target, cmd, option, true);
        } catch (std::exception &) {

        }
        cmd = "mkdir";
      }
      cmd += " ";
      cmd += escaped_path;
    }

    if (cmd != "mkdir") {
      try {
        co_await co_execute_shell(target, cmd, option, true);
      } catch (std::exception &) {

      }
    }
  }

  for (const copyinfo& ci : file_list) {
    co_await sync_send(socket, ci.rpath, ci.lpath, ci.mode, ci.time);
  }
}

struct ScopedSyncConnect {
  tcp::socket socket;

  ScopedSyncConnect(tcp::socket &&s) : socket(std::move(s)) {}

  ScopedSyncConnect(const ScopedSyncConnect &) = delete;
  ScopedSyncConnect(ScopedSyncConnect &&) = default;

  awaitable<void>
  end() {
    co_await sync_send_request(socket, ID_QUIT, "");
  }
};

awaitable<ScopedSyncConnect>
sync_open_connect(tcp::endpoint target, TransportOption option) {
  auto client = co_await connect(
        target,
        "sync:",
        option,
        nullptr);
  co_return ScopedSyncConnect{std::move(client)};
}

} // namespace


bool Stat::isExe() const {
  return S_ISEXE(mode);
}

awaitable<std::vector<ListItem>>
co_sync_list(std::string_view path, TransportOption option) {
  auto target = co_await resolve_endpoint(option);
  auto features = co_await co_get_features(target, option);
  bool have_ls_v2 = std::ranges::find(features, "ls_v2") != features.end();

  auto client = co_await sync_open_connect(
        target,
        option);
  
  auto res = co_await sync_list(client.socket, path, have_ls_v2);

  co_await client.end();

  co_return res;
}

awaitable<void>
co_sync_pull(
    const std::vector<std::string>& srcs,
    const std::filesystem::path &dst,
    TransportOption option) {
  auto target = co_await resolve_endpoint(option);
  auto features = co_await co_get_features(target, option);
  bool have_stat_v2 = std::ranges::find(features, "stat_v2") != features.end();
  bool have_ls_v2 = std::ranges::find(features, "ls_v2") != features.end();

  auto client = co_await sync_open_connect(
        target,
        option);

  std::error_code ec;
  bool dst_exists = std::filesystem::exists(dst, ec);
  bool dst_isdir = dst_exists && std::filesystem::is_directory(dst, ec);

  if (!dst_exists) {
    // If we're only pulling one path, the destination path might point to
    // a path that doesn't exist yet.
    if (srcs.size() == 1 && errno == ENOENT) {
      // However, its parent must exist.
      std::error_code ec;
      if (!std::filesystem::exists(dst.parent_path(), ec)) {
        throw adb_sync_error("cannot create file/directory " + dst.string(), -1);
      }
    } else {
      throw adb_sync_error("failed to access " + dst.string(), -1);
    }
  }

  
  if (!dst_isdir) {
    if (srcs.size() > 1) {
      throw adb_sync_error(std::format("target '{}' is not a directory", dst.string()), -1);
    }
  }

  for (const auto & src_path : srcs) {
    auto src_st = co_await sync_stat(client.socket, src_path, have_stat_v2);
    bool src_isdir = S_ISDIR(src_st.mode);
  
    if (src_isdir) {
      auto dst_dir = dst;

      // If the destination path existed originally, the source directory
      // should be copied as a child of the destination.
      if (dst_exists) {
        if (!dst_isdir) {
          throw adb_sync_error(std::format("target '{}' is not a directory", dst.string()), -1);
        }
        dst_dir = dst_dir / posix_basename(src_path);
      }

      co_await copy_remote_dir_local(client.socket, src_path, dst_dir, have_stat_v2, have_ls_v2);
    } else if (S_ISREG(src_st.mode)) {
      auto dst_path = dst;
      if (dst_isdir) {
        // If we're copying a remote file to a local directory, we
        // really want to copy to local_dir + OS_PATH_SEPARATOR +
        // basename(remote).
        dst_path = dst_path / posix_basename(src_path);
      }
      co_await sync_recv(client.socket, src_path, dst_path);
    }
  }

  co_await client.end();
}

awaitable<std::vector<char>>
co_sync_pull_buffer(
    std::string_view src,
    TransportOption option) {
  auto target = co_await resolve_endpoint(option);
  auto features = co_await co_get_features(target, option);
  bool have_stat_v2 = std::ranges::find(features, "stat_v2") != features.end();

  auto client = co_await sync_open_connect(
        target,
        option);

  auto st = co_await sync_stat(client.socket, src, have_stat_v2);

  if (S_ISDIR(st.mode)) {
    throw adb_sync_error(std::format("target '{}' is a directory", src), -1);
  }

  auto buffer = co_await sync_recv_buffer(client.socket, src);

  co_await client.end();
  co_return buffer;
}

awaitable<Stat>
co_sync_stat(
    std::string_view path,
    TransportOption option) {
  auto target = co_await resolve_endpoint(option);
  auto features = co_await co_get_features(target, option);
  bool have_stat_v2 = std::ranges::find(features, "stat_v2") != features.end();

  auto client = co_await sync_open_connect(
        target,
        option);
  
  co_return co_await sync_stat(client.socket, path, have_stat_v2);
}

awaitable<void>
co_sync_push(
    const std::vector<LocalPath>& srcs,
    std::string_view dst,
    TransportOption option) {
  auto target = co_await resolve_endpoint(option);
  auto features = co_await co_get_features(target, option);
  bool have_stat_v2 = std::ranges::find(features, "stat_v2") != features.end();
  bool have_fixed_push_mkdir = std::ranges::find(features, "fixed_push_mkdir") != features.end();
  bool have_shell_v2 = std::ranges::find(features, "shell_v2") != features.end();

  auto client = co_await sync_open_connect(
        target,
        option);

  bool dst_exists = false;
  bool dst_isdir = false;

  try {
    auto st = co_await sync_stat(client.socket, dst, have_stat_v2);
    dst_exists = true;
    dst_isdir = S_ISDIR(st.mode);
  } catch (std::exception &) {

  }

  if (!dst_isdir) {
    if (srcs.size() > 1) {
      throw adb_sync_error(std::format("target '{}' is not a directory", dst), -1);
    } else {
      // A path that ends with a slash doesn't have to be a directory if
      // it doesn't exist yet.
      if (dst.back() == '/' && dst_exists) {
        throw adb_sync_error(std::format("failed to access '{}': Not a directory", dst), -1);
      }
    }
  }

  for (const auto &src_path : srcs) {
    struct stat st;
    if (stat(src_path.string().c_str(), &st) == -1) {
      continue;
    }

    if (S_ISDIR(st.st_mode)) {
      auto dst_dir = std::string(dst);

      // If the destination path existed originally, the source directory
      // should be copied as a child of the destination.
      if (dst_exists) {
        if (!dst_isdir) {
          throw adb_sync_error(std::format("target '{}' is not a directory", dst), -1);
        }
        // dst is a POSIX path, so we don't want to use the sysdeps
        // helpers here.
        dst_dir = posix_join(dst_dir, src_path.filename().string());
      }

      co_await copy_local_dir_remote(
          client.socket,
          src_path,
          dst_dir,
          have_fixed_push_mkdir,
          have_shell_v2,
          target,
          option);
      continue;
    } else if (S_ISREG(st.st_mode)) {
      auto dst_path = std::string(dst);
      if (dst_isdir) {
        // If we're copying a local file to a remote directory,
        // we really want to copy to remote_dir + "/" + local_filename.
        dst_path = posix_join(dst_path, src_path.filename().string());
      }

      co_await sync_send(client.socket, dst_path, src_path, st.st_mode, st.st_mtime);
    }
  }

  co_await client.end();
}

awaitable<void>
co_sync_push_buffer(
    const char *buffer,
    size_t size,
    std::string_view dst,
    TransportOption option) {
  auto target = co_await resolve_endpoint(option);
  auto features = co_await co_get_features(target, option);
  bool have_stat_v2 = std::ranges::find(features, "stat_v2") != features.end();

  auto client = co_await sync_open_connect(
        target,
        option);

  bool dst_isdir = false;

  try {
    auto st = co_await sync_stat(client.socket, dst, have_stat_v2);
    dst_isdir = S_ISDIR(st.mode);
  } catch (std::exception &) {

  }

  if (dst_isdir) {
    throw adb_sync_error(std::format("target '{}' is a directory", dst), -1);
  }

  co_await sync_send_buffer(client.socket, dst, buffer, size);

  co_await client.end();
}

} // namespace adb_client
