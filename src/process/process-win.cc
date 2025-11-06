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

#include <windows.h>
#include <cstring>
#include <TlHelp32.h>
#include <stdexcept>

namespace process_lib {

template <>
void maybe_quote_arg<char>(std::string& arg) {
  auto it = arg.find_first_of(" \t\""); // contains space or double quotes?
  if(it != arg.npos) {
    // double existing quotes
    string_replace_all<char>(arg, "\"", "\"\"");
    // surround with quotes
    arg.insert(arg.begin(), '"');
    arg += '"';
  }
}

template <>
void maybe_quote_arg<wchar_t>(std::wstring& arg) {
  auto it = arg.find_first_of(L" \t\""); // contains space or double quotes?
  if(it != arg.npos) {
    // double existing quotes
    string_replace_all<wchar_t>(arg, L"\"", L"\"\"");
    // surround with quotes
    arg.insert(arg.begin(), L'"');
    arg += L'"';
  }
}


std::vector<std::filesystem::path>
get_sys_paths() {
  std::vector<std::filesystem::path> out;

  auto *env = GetEnvironmentStringsW();
  if (env) {
    wchar_t *p = env;
    wchar_t *paths = nullptr;
  
    if (*p != 0) {
      if (wcsncmp(p, L"Path=", 5) == 0 || wcsncmp(p, L"PATH=", 5) == 0) {
        paths = p + 5;
      }

      while (!paths && (*p != 0) || (*(p+1) != 0)) {
        if (*p == 0) {
          p++;
          
          if (wcsncmp(p, L"Path=", 5) == 0 || wcsncmp(p, L"PATH=", 5) == 0) {
            paths = p + 5;
          }
        } else
          p++;
      }
    }

    if (paths) {
      for (;;) {
        wchar_t *next = wcschr(paths, L';');
        if (next) {
          out.push_back(std::filesystem::path(std::wstring(paths, next)));
        } else {
          out.push_back(std::filesystem::path(std::wstring(paths)));
          break;
        }
        paths = next + 1;
      }
    }

    FreeEnvironmentStringsW(env);
  }

  return out;
}

Process::FileHandle::~FileHandle() {
  ::CloseHandle(fd_);
}

bool Process::FileHandle::read(void *buf, size_t size, size_t &got) noexcept {
  DWORD n;
  BOOL bSuccess = ::ReadFile(fd_, static_cast<CHAR*>(buf), static_cast<DWORD>(size), &n, nullptr);
  if(!bSuccess || n == 0) {
    return false;
  }

  got = static_cast<size_t>(n);
  return true;
}

bool Process::FileHandle::write(const char *bytes, size_t n) noexcept {
  DWORD written;
  BOOL bSuccess = ::WriteFile(fd_, bytes, (DWORD)n, &written, nullptr);
  if(!bSuccess || written == 0) {
    return false;
  } else {
    return true;
  }
}

// Simple HANDLE wrapper to close it automatically from the destructor.
class Handle {
public:
  Handle() noexcept : handle(INVALID_HANDLE_VALUE) { }
  ~Handle() noexcept {
    close();
  }
  void close() noexcept {
    if (handle != INVALID_HANDLE_VALUE)
      CloseHandle(handle);
  }
  HANDLE detach() noexcept {
    HANDLE old_handle = handle;
    handle = INVALID_HANDLE_VALUE;
    return old_handle;
  }
  operator HANDLE() const noexcept { return handle; }
  HANDLE* operator&() noexcept { return &handle; }
private:
  HANDLE handle;
};

//Based on the discussion thread: https://www.reddit.com/r/cpp/comments/3vpjqg/a_new_platform_independent_process_library_for_c11/cxq1wsj
std::mutex create_process_mutex;

//Based on the example at https://msdn.microsoft.com/en-us/library/windows/desktop/ms682499(v=vs.85).aspx.
bool Process::open(
    std::vector<string_type> &&args,
    const string_type &path,
    bool open_stdout,
    bool open_stderr,
    bool open_stdin,
    uint32_t features) noexcept {
  Handle stdin_rd_p;
  Handle stdin_wr_p;

  Handle stdout_rd_p;
  Handle stdout_wr_p;

  Handle stderr_rd_p;
  Handle stderr_wr_p;

  SECURITY_ATTRIBUTES security_attributes;

  security_attributes.nLength = sizeof(SECURITY_ATTRIBUTES);
  security_attributes.bInheritHandle = TRUE;
  security_attributes.lpSecurityDescriptor = nullptr;

  std::lock_guard<std::mutex> lock(create_process_mutex);

  if(open_stdin) {
    if (!CreatePipe(&stdin_rd_p, &stdin_wr_p, &security_attributes, 0) ||
        !SetHandleInformation(stdin_wr_p, HANDLE_FLAG_INHERIT, 0))
    return false;
  }

  if(open_stdout) {
    if (!CreatePipe(&stdout_rd_p, &stdout_wr_p, &security_attributes, 0) ||
        !SetHandleInformation(stdout_rd_p, HANDLE_FLAG_INHERIT, 0)) {
      return false;
    }
  }

  if(open_stderr) {
    if (!CreatePipe(&stderr_rd_p, &stderr_wr_p, &security_attributes, 0) ||
        !SetHandleInformation(stderr_rd_p, HANDLE_FLAG_INHERIT, 0)) {
      return false;
    }
  }

  PROCESS_INFORMATION process_info;
  STARTUPINFOW startup_info;

  ZeroMemory(&process_info, sizeof(PROCESS_INFORMATION));

  ZeroMemory(&startup_info, sizeof(STARTUPINFO));
  startup_info.cb = sizeof(STARTUPINFO);
  startup_info.hStdInput = stdin_rd_p;
  startup_info.hStdOutput = stdout_wr_p;
  startup_info.hStdError = stderr_wr_p;
  if(open_stdout || open_stderr || open_stdin)
    startup_info.dwFlags |= STARTF_USESTDHANDLES;

  std::filesystem::path exe;
  std::vector<std::wstring> out_args;

  parse_command_args(std::move(args), exe, out_args);

  auto cmdline = build_args(std::move(out_args));

  DWORD flags = CREATE_NO_WINDOW;
  if (features & FLAGS_CONSOLE_WINDOW) {
    flags &= ~CREATE_NO_WINDOW;
  }

  BOOL bSuccess = CreateProcessW(
      exe.c_str(),
      (LPWSTR)cmdline.c_str(),
      nullptr,
      nullptr,
      TRUE,
      flags,
      nullptr,
      path.empty()?nullptr:path.c_str(),
      &startup_info,
      &process_info);

  if(!bSuccess)
    return false;
  
  CloseHandle(process_info.hThread);

  if(open_stdout) { 
    stdoutFd_ = std::make_unique<FileHandle>(stdout_rd_p.detach());
  }

  if(open_stderr) {
    stderrFd_ = std::make_unique<FileHandle>(stderr_rd_p.detach());
  }

  if(open_stdin) {
    stdinFd_ = std::make_unique<FileHandle>(stdin_wr_p.detach());
  }

  dwProcessId_ = process_info.dwProcessId;
  procHandle_ = process_info.hProcess;

  return true;
}

//Based on http://stackoverflow.com/a/1173396
void Process::kill() noexcept {
  std::lock_guard<std::mutex> lock(close_mutex);
  if(dwProcessId_ > 0 && !closed_) {
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if(snapshot) {
      PROCESSENTRY32 process;
      ZeroMemory(&process, sizeof(process));
      process.dwSize = sizeof(process);
      if(Process32First(snapshot, &process)) {
        do {
          if (process.th32ParentProcessID == dwProcessId_) {
            HANDLE process_handle = OpenProcess(PROCESS_TERMINATE, FALSE, process.th32ProcessID);
            if(process_handle) {
              TerminateProcess(process_handle, 2);
              CloseHandle(process_handle);
            }
          }
        } while (Process32Next(snapshot, &process));
      }
      CloseHandle(snapshot);
    }
    TerminateProcess(procHandle_, 2);
  }
}

int Process::wait() noexcept {
  if(dwProcessId_ == 0 || procHandle_ == nullptr)
    return -1;

  DWORD exit_status;
  WaitForSingleObject(procHandle_, INFINITE);
  if(!GetExitCodeProcess(procHandle_, &exit_status))
    exit_status = -1;

  closeHandles();

  return static_cast<int>(exit_status);
}

bool Process::wait(int &status, long ms) noexcept {
  if(dwProcessId_ == 0 || procHandle_ == nullptr)
    return false;

  DWORD wait_status = WaitForSingleObject(procHandle_, ms);

  if (wait_status == WAIT_TIMEOUT)
    return false;

  DWORD exit_status_win;
  if(!GetExitCodeProcess(procHandle_, &exit_status_win))
    exit_status_win = -1;

  closeHandles();

  status = static_cast<int>(exit_status_win);
  return true;
}

void Process::closeProcessHandle() noexcept {
  if (procHandle_) {
    ::CloseHandle(procHandle_);
    procHandle_ = nullptr;
  }
}

namespace {

std::vector<std::wstring>
convectToWargs(const std::vector<std::string> &args) {
  std::vector<std::wstring> wargs;
  for (auto &arg : args) {
    wargs.push_back(std::filesystem::path(arg).wstring());
  }
  return wargs;
}

} // namespace

Process::Process(
    std::vector<std::string> &&args,
    const std::string &workDir,
    std::function<void(const char* bytes, size_t n)> &&read_stdout,
    std::function<void(const char* bytes, size_t n)> &&read_stderr,
    uint32_t features,
    bool detach,
    bool open_input,
    size_t buffer_size) noexcept
    : Process(
      convectToWargs(args),
      std::filesystem::path(workDir).wstring(),
      std::move(read_stdout),
      std::move(read_stderr),
      features,
      detach,
      open_input,
      buffer_size) {}

Process::Process(
    std::vector<std::string> &&args,
    const std::string &workDir,
    uint32_t features,
    bool detach) noexcept
  : Process(
    convectToWargs(args),
    std::filesystem::path(workDir).wstring(),
    features,
    detach) {}

Process::Process(
    std::vector<std::string> &&args, 
    const std::string &workDir,
    ProcessOutputReader &read_stdout,
    ProcessOutputReader &read_stderr,
    uint32_t features,
    bool open_input) noexcept
  : Process(
    convectToWargs(args),
    std::filesystem::path(workDir).wstring(),
    read_stdout,
    read_stderr,
    features,
    open_input
  ) {}

} // namespace process_lib
