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
#include <asio.hpp>

namespace adb_client {

using asio::io_context;
using asio::awaitable;

namespace {

template <typename FUNC, typename... Args>
void co_spawn_run(FUNC f, Args&&... args) {
  io_context ctx;

  co_spawn(
    ctx,
    f(std::forward<Args>(args)...),
    [](std::exception_ptr e) {
      if (e)
       std::rethrow_exception(e);
    });

  ctx.run();
}

template <typename TOUT, typename FUNC, typename... Args>
TOUT co_spawn_run_ret(FUNC f, Args&&... args) {
  io_context ctx;
  TOUT result;

  auto f_wrap = [f](TOUT &out, Args&&... args) -> awaitable<void> {
    out = co_await f(std::forward<Args>(args)...);
  };

  co_spawn(
    ctx,
    f_wrap(result, std::forward<Args>(args)...),
    [](std::exception_ptr e) {
      if (e)
       std::rethrow_exception(e);
    });

  ctx.run();

  return result;
}

} // namespace

void adb_kill(TransportOption option) noexcept {
  co_spawn_run(co_kill, option);
}

void adb_command(
    std::string_view command,
    TransportOption option,
    std::optional<std::chrono::milliseconds> timeout) {
  co_spawn_run(co_command, command, option, timeout);
}

std::string
adb_query(
    std::string_view service,
    TransportOption option) {
  return co_spawn_run_ret<std::string>(co_query, service, option);
}

std::string
adb_command_query(
    std::string_view command,
    TransportOption option) {
  return co_spawn_run_ret<std::string>(co_command_query, command, option);
}

void wait_device(
    std::string_view state,
    TransportOption option,
    std::optional<std::chrono::milliseconds> timeout) {
  co_spawn_run(co_wait_device, state, option, timeout);
}

std::vector<std::string>
adb_get_features(
    TransportOption option) {
  return co_spawn_run_ret<std::vector<std::string>>(co_get_features, option);
}

std::vector<char>
adb_command_connect(
    std::string_view command,
    TransportOption option) {
  return co_spawn_run_ret<std::vector<char>>(co_command_connect, command, option);
}

std::tuple<uint8_t, std::vector<char>, std::vector<char>>
adb_execute_shell(
    std::string_view command,
    TransportOption option,
    std::optional<bool> use_shell_protocol) {
  return co_spawn_run_ret<std::tuple<uint8_t, std::vector<char>, std::vector<char>>>(co_execute_shell, command, option, use_shell_protocol);
}

void adb_remount(
    TransportOption option,
    std::optional<bool> use_remount_shell,
    std::string_view args) {
  co_spawn_run(co_remount, option, use_remount_shell, args);
}

void adb_root(bool root, TransportOption option) {
  co_spawn_run(co_root, root, option);
}

std::vector<DeviceInfo>
adb_list_devices(
    TransportOption option,
    bool device_only, std::string_view target_serial) {
  return co_spawn_run_ret<std::vector<DeviceInfo>>(co_list_devices, option, device_only, target_serial);
}

Stat
sync_stat(
    std::string_view path,
    TransportOption option) {
  return co_spawn_run_ret<Stat>(co_sync_stat, path, option);
}

std::vector<ListItem>
sync_list(
    std::string_view path,
    TransportOption option) {
  return co_spawn_run_ret<std::vector<ListItem>>(co_sync_list, path, option);
}

void sync_pull(
    const std::vector<std::string>& srcs,
    const std::filesystem::path &dst,
    TransportOption option) {
  co_spawn_run(co_sync_pull, srcs, dst, option);
}

std::vector<char> sync_pull_buffer(
    std::string_view path,
    TransportOption option) {
  return co_spawn_run_ret<std::vector<char>>(co_sync_pull_buffer, path, option);
}

void sync_push(
    const std::vector<std::filesystem::path>& srcs,
    std::string_view dst,
    TransportOption option) {
  co_spawn_run(co_sync_push, srcs, dst, option);
}

void sync_push_buffer(
    const char *buffer,
    size_t size,
    std::string_view dst,
    TransportOption option) {
  co_spawn_run(co_sync_push_buffer, buffer, size, dst, option);
}

} // namespace adb_client
