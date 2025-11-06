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

#pragma  once

#include <string>
#include <functional>
#include <vector>
#include <mutex>
#include <thread>
#include <memory>
#ifndef _WIN32
#include <sys/wait.h>
#endif
#include <filesystem>
#include <tuple>
#include <unordered_map>
#include <cstdint>

namespace process_lib {

class ProcessOutputReader {
public:
  virtual ~ProcessOutputReader() {} 
  virtual void *allocateReadBuffer(size_t &buffer_size) = 0;
  virtual void commitReadBuffer(size_t count) = 0;
};

class Process {
public:
#ifdef _WIN32
  typedef void *fd_type;
  typedef std::wstring string_type;
#else
  typedef int fd_type;
  typedef std::string string_type;
#endif

private:
  class FileHandle {
  public:
    FileHandle(fd_type);
    FileHandle(const FileHandle&) = delete;
    FileHandle(FileHandle&&) = delete;
    ~FileHandle();

    bool read(void *buf, size_t size, size_t &got) noexcept;
    bool write(const char *bytes, size_t n) noexcept;
  
  private:
    fd_type fd_;
  };

public:
  enum {
    FLAGS_CONSOLE_WINDOW = 1,
  };

  static constexpr int TIMEOUT_ERROR = 1235;

  ///Note on Windows: it seems not possible to specify which pipes to redirect.
  ///Thus, at the moment, if read_stdout==nullptr, read_stderr==nullptr and open_stdin==false,
  ///the stdout, stderr and stdin are sent to the parent process instead.
  Process(
    std::vector<string_type> &&args, 
    const string_type &workDir,
    std::function<void(const char *bytes, size_t n)> &&read_stdout,
    std::function<void(const char *bytes, size_t n)> &&read_stderr,
    uint32_t features = 0,
    bool detach = false,
    bool open_input = false,
    size_t buffer_size = 65536) noexcept;

  // shortter constructor
  Process(
    std::vector<string_type> &&args, 
    const string_type &workDir = string_type(),
    uint32_t features = 0,
    bool detach = false) noexcept;

  Process(
    std::vector<string_type> &&args, 
    const string_type &workDir,
    ProcessOutputReader &read_stdout,
    ProcessOutputReader &read_stderr,
    uint32_t features = 0,
    bool open_input = false) noexcept;

#ifdef _WIN32
  Process(
    std::vector<std::string> &&args,
    const std::string &workDir,
    std::function<void(const char *bytes, size_t n)> &&read_stdout,
    std::function<void(const char *bytes, size_t n)> &&read_stderr,
    uint32_t features = 0,
    bool detach = false,
    bool open_input = false,
    size_t buffer_size = 65536) noexcept;

  Process(
    std::vector<std::string> &&args,
    const std::string &workDir = std::string(),
    uint32_t features = 0,
    bool detach = false) noexcept;

  Process(
    std::vector<std::string> &&args, 
    const std::string &workDir,
    ProcessOutputReader &read_stdout,
    ProcessOutputReader &read_stderr,
    uint32_t features = 0,
    bool open_input = false) noexcept;
#endif

  ~Process() noexcept;

  void kill() noexcept;
  int wait() noexcept;
  bool wait(int &status, long ms) noexcept;

  bool write(const char *bytes, size_t n) noexcept;

private:
#ifdef _WIN32
  void * procHandle_{nullptr};
  unsigned long dwProcessId_{0};
#else
  pid_t pid_{-1};
#endif
  bool closed_{true};

  std::mutex close_mutex;
  std::mutex stdin_mutex;
  std::thread stdout_thread, stderr_thread;

  std::unique_ptr<FileHandle> stdoutFd_;
  std::unique_ptr<FileHandle> stderrFd_;
  std::unique_ptr<FileHandle> stdinFd_;

  bool open(
    std::vector<string_type> &&args,
    const string_type &path,
    bool open_stdout,
    bool open_stderr,
    bool open_stdin,
    uint32_t features) noexcept;

  void asyncRead(
    std::function<void(const char* bytes, size_t n)> &&read_stdout,
    std::function<void(const char* bytes, size_t n)> &&read_stderr,
    size_t buffer_size) noexcept;

  void asyncRead(
    ProcessOutputReader &read_stdout,
    ProcessOutputReader &read_stderr) noexcept;

  void closeFDs() noexcept;
  void closeHandles() noexcept;
  void closeProcessHandle() noexcept;
};

std::vector<std::filesystem::path>
get_sys_paths();

std::filesystem::path
search_exe_path(const std::filesystem::path &exe, const std::vector<std::filesystem::path> &sys_paths);

std::filesystem::path
search_exe_path(const std::filesystem::path &exe);

template <typename CharT>
void maybe_quote_arg(std::basic_string<CharT>& arg);

template <typename CharT>
std::basic_string<CharT> build_args(std::vector<std::basic_string<CharT>> && data) {
  std::basic_string<CharT> st;

  for(auto & arg : data) {
    if(!arg.empty()) {
      maybe_quote_arg<CharT>(arg);

      if (!st.empty())
        st += CharT(' ');

      st += arg;
    }
  }

  return st;
}

template<typename CharT>
inline void
parse_shell_command_args(
    std::vector<std::basic_string<CharT>> &&args,
    std::filesystem::path &exe,
    std::vector<std::basic_string<CharT>> &out_args,
    const std::vector<std::filesystem::path> &sys_paths)
{
  if (args.empty()) {
    return;
  }

#if defined(_WIN32)
  exe = search_exe_path(L"cmd.exe", sys_paths);
  out_args.push_back(L"cmd");
  out_args.push_back(L"/c");
#else
  exe = "/bin/sh";
  out_args.push_back("sh");
  out_args.push_back("-c");
#endif
  out_args.push_back(build_args(std::move(args)));
}

template<typename CharT>
inline void
parse_command_args(std::vector<std::basic_string<CharT>> &&args, std::filesystem::path &exe, std::vector<std::basic_string<CharT>> &out_args)
{
  if (args.empty()) {
    return;
  }

  auto sys_paths = get_sys_paths();

  bool has_pipe = std::ranges::any_of(args, [](auto &arg) {
    return (arg.size() == 2 && arg[0] == CharT('&') && arg[1] == CharT('&')) ||
      (arg.size() == 1 && arg[0] == CharT('|'));
  });

  bool execute_in_shell = has_pipe;
  // arg0.extension() == ".py"
  if (auto n = args[0].size();
          n > 4 && 
          args[0][n - 4] == CharT('.') && 
          args[0][n - 3] == CharT('b') && 
          args[0][n - 2] == CharT('a') && 
          args[0][n - 1] == CharT('t')) {
    execute_in_shell = true;
  } else if (auto n = args[0].size(); 
          n > 4 && 
          args[0][n - 4] == CharT('.') && 
          args[0][n - 3] == CharT('c') && 
          args[0][n - 2] == CharT('m') && 
          args[0][n - 1] == CharT('d')) {
    execute_in_shell = true;
  } else if (auto n = args[0].size(); 
          n > 3 && 
          args[0][n - 3] == CharT('.') && 
          args[0][n - 2] == CharT('s') && 
          args[0][n - 1] == CharT('h')) {
    execute_in_shell = true;
  }

  if (execute_in_shell) {
    parse_shell_command_args(std::move(args), exe, out_args, sys_paths);
    return;
  }

  // arg0.extension() == ".py"
  if (auto n = args[0].size(); 
          n > 3 && 
          args[0][n - 3] == CharT('.') && 
          args[0][n - 2] == CharT('p') && 
          args[0][n - 1] == CharT('y')) {
    auto py_args = std::move(args);

    if (auto py = search_exe_path("python3", sys_paths); !py.empty()) {
      py_args.insert(py_args.begin(), py.c_str());
    } else {
      py_args.insert(py_args.begin(), std::filesystem::path("python").c_str());
    }

    parse_command_args(std::move(py_args), exe, out_args);
    return;
  }

  exe = search_exe_path(args[0], sys_paths);
  if (exe.empty()) {
    parse_shell_command_args(std::move(args), exe, out_args, sys_paths);
  } else {
    out_args = std::move(args);
  }
}

//
// this api is to workaround some times 
// some process will blocked on ReadFile/CloseHandle
// 
int
executeScriptNoOutput(
    const char *script,
    const std::vector<std::string> &vargs = {},
    const std::unordered_map<std::string, std::string> &kwargs = {},
    long timeoutsMs = -1,
    const std::string &workDir = std::string()) noexcept;

std::tuple<int, std::string, std::string> 
executeScript(
    const char *script,
    const std::vector<std::string> &vargs = {},
    const std::unordered_map<std::string, std::string> &kwargs = {},
    long timeoutsMs = -1,
    const std::string &workDir = std::string()) noexcept;

void
spawn(
    const char *script,
    const std::vector<std::string> &vargs = {},
    const std::unordered_map<std::string, std::string> &kwargs = {},
    const std::string &workDir = std::string(),
    uint32_t features = 0) noexcept;

#ifdef _WIN32

int
executeScriptNoOutput(
    const wchar_t *script,
    const std::vector<std::wstring> &vargs = {},
    const std::unordered_map<std::wstring, std::wstring> &kwargs = {},
    long timeoutsMs = -1,
    const std::wstring &workDir = std::wstring()) noexcept;

std::tuple<int, std::string, std::string> 
executeScript(
    const wchar_t *script,
    const std::vector<std::wstring> &vargs = {},
    const std::unordered_map<std::wstring, std::wstring> &kwargs = {},
    long timeoutsMs = -1,
    const std::wstring &workDir = std::wstring()) noexcept;

void
spawn(
    const wchar_t *script,
    const std::vector<std::wstring> &vargs = {},
    const std::unordered_map<std::wstring, std::wstring> &kwargs = {},
    const std::wstring &workDir = std::wstring(),
    uint32_t features = 0) noexcept;

#endif

std::string executeScriptGetResult(
    const char *script,
    const std::vector<std::string> &vargs = {},
    const std::unordered_map<std::string, std::string> &kwargs = {},
    long timeoutsMs = -1,
    const std::string &workDir = std::string()) noexcept;


#if __linux__ 

bool runingAsSudoer();

#endif

} // namespace process_lib

