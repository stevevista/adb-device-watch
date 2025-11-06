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

#include <cstdlib>
#include <unistd.h>
#include <signal.h>
#include <stdexcept>

namespace process_lib {

template <>
void maybe_quote_arg<char>(std::string& arg) {
  string_replace_all<char>(arg, "\"", "\\\"");

  auto it = arg.find_first_of(" \t"); // contains space?
  if(it != arg.npos) {
    // surround with quotes
    arg.insert(arg.begin(), '"');
    arg += '"';
  }
}

template <>
void maybe_quote_arg<wchar_t>(std::wstring& arg) {
  string_replace_all<wchar_t>(arg, L"\"", L"\\\"");

  auto it = arg.find_first_of(L" \t"); // contains space?
  if(it != arg.npos) {
    // surround with quotes
    arg.insert(arg.begin(), L'"');
    arg += L'"';
  }
}


std::vector<std::filesystem::path>
get_sys_paths() {
  std::vector<std::filesystem::path> out;

  const auto *paths = ::getenv("PATH");
  if (paths) {
    for (;;) {
      const char *next = strchr(paths, ':');
      if (next) {
        out.push_back(std::filesystem::path(std::string(paths, next)));
      } else {
        out.push_back(std::filesystem::path(std::string(paths)));
        break;
      }
      paths = next + 1;
    }
  }

  return out;
}

Process::FileHandle::~FileHandle() {
  ::close(fd_);
}

bool Process::FileHandle::read(void *buf, size_t size, size_t &got) noexcept {
  ssize_t n = ::read(fd_, buf, size);
  if (n <= 0) {
    return false;
  }

  got = n;
  return true;
}

bool Process::FileHandle::write(const char *bytes, size_t n) noexcept {
  if(::write(fd_, bytes, n)>=0) {
    return true;
  } else {
    return false;
  }
}

bool Process::open(
    std::vector<string_type> &&args,
    const std::string &path,
    bool open_stdout,
    bool open_stderr,
    bool open_stdin,
    uint32_t features) noexcept {
  int stdin_p[2], stdout_p[2], stderr_p[2];
  

  if(open_stdin && ::pipe(stdin_p) != 0)
    return false;

  if(open_stdout && ::pipe(stdout_p) != 0) {
    if (open_stdin) { ::close(stdin_p[0]); ::close(stdin_p[1]); }
    return false;
  }

  if(open_stderr && ::pipe(stderr_p) != 0) {
    if (open_stdin) { ::close(stdin_p[0]); ::close(stdin_p[1]); }
    if(open_stdout) { ::close(stdout_p[0]); ::close(stdout_p[1]); }
    return false;
  }
  
  auto pid = fork();
  
  if (pid < 0) {
    if (open_stdin) { ::close(stdin_p[0]); ::close(stdin_p[1]); }
    if(open_stdout) { ::close(stdout_p[0]); ::close(stdout_p[1]); }
    if(open_stderr) { ::close(stderr_p[0]); ::close(stderr_p[1]); }
    return false;
  }
  else if (pid == 0) {
    if (open_stdin) ::dup2(stdin_p[0], 0);
    if (open_stdout) ::dup2(stdout_p[1], 1);
    if (open_stderr) ::dup2(stderr_p[1], 2);
    if (open_stdin) { ::close(stdin_p[0]); ::close(stdin_p[1]); }
    if (open_stdout) { ::close(stdout_p[0]); ::close(stdout_p[1]); }
    if (open_stderr) { ::close(stderr_p[0]); ::close(stderr_p[1]); }
  
    //Based on http://stackoverflow.com/a/899533/3808293
    int fd_max=static_cast<int>(sysconf(_SC_OPEN_MAX)); // truncation is safe
    for(int fd=3;fd<fd_max;fd++)
      close(fd);
  
    setpgid(0, 0);
    //TODO: See here on how to emulate tty for colors: http://stackoverflow.com/questions/1401002/trick-an-application-into-thinking-its-stdin-is-interactive-not-a-pipe
    //TODO: One solution is: echo "command;exit"|script -q /dev/null

    std::filesystem::path exe;
    std::vector<std::string> out_args;

    parse_command_args(std::move(args), exe, out_args);

    std::vector<const char *> c_args(out_args.size() + 1);
    for (size_t i = 0; i < out_args.size(); i++) {
      c_args[i] = out_args[i].c_str();
    }

    if (!path.empty()) {
      ::chdir(path.c_str());
    }

    execv(exe.c_str(), (char *const *)&c_args[0]);
    
    _exit(EXIT_FAILURE);
  }
  
  if (open_stdin) ::close(stdin_p[0]);
  if (open_stdout) ::close(stdout_p[1]);
  if (open_stderr) ::close(stderr_p[1]);

  if (open_stdin) {
    stdinFd_ = std::make_unique<FileHandle>(stdin_p[1]);
  }

  if (open_stdout) {
    stdoutFd_ = std::make_unique<FileHandle>(stdout_p[0]);
  }

  if (open_stderr) {
    stderrFd_ = std::make_unique<FileHandle>(stderr_p[0]);
  }

  pid_ = pid;

  return true;
}

void Process::kill() noexcept {
  std::lock_guard lock(close_mutex);
  if(pid_ > 0 && !closed_) {
    ::kill(-pid_, SIGTERM);
  }
}

int Process::wait() noexcept {
  if(pid_ <= 0)
    return -1;

  int exit_status;
  waitpid(pid_, &exit_status, 0);
  
  closeHandles();

  if(exit_status >= 256)
    exit_status = exit_status>>8;
  return exit_status;
}

bool Process::wait(int &status, long ms) noexcept {
  if(pid_ <= 0)
    return false;

  int ret;
  ::sigset_t sigset;

  //I need to set the signal, because it might be ignore / default, in which case sigwait might not work.

  using _signal_t = void(*)(int);
  static thread_local _signal_t sigchld_handler = SIG_DFL;

  struct signal_interceptor_t {
    static void handler_func(int val)
    {
            if ((sigchld_handler != SIG_DFL) && (sigchld_handler != SIG_IGN))
                sigchld_handler(val);
    }
    signal_interceptor_t()  { sigchld_handler = ::signal(SIGCHLD, &handler_func); }
    ~signal_interceptor_t() { ::signal(SIGCHLD, sigchld_handler); sigchld_handler = SIG_DFL;}

  } signal_interceptor{};

  if (sigemptyset(&sigset) != 0) {
    return false;
  }
    
  if (sigaddset(&sigset, SIGCHLD) != 0) {
    return false;
  }

  struct ::sigaction old_sig;
  if (-1 == ::sigaction(SIGCHLD, nullptr, &old_sig)) {
    return false;
  }

  long long ns = (long long)ms * 1000000;
  const auto begin = std::chrono::steady_clock::now();

  do {
    auto span = (long long)std::chrono::duration_cast<std::chrono::nanoseconds>
        	(std::chrono::steady_clock::now() - begin).count();

    ns -= span;

    ::timespec ts;
    ts.tv_sec  = ns <= 0 ? 0 : ns / 1000000000;
    ts.tv_nsec = ns <= 0 ? 0 : ns % 1000000000;

    auto ret_sig = ::sigtimedwait(&sigset, nullptr, &ts);
    errno = 0;
    ret = ::waitpid(pid_, &status, WNOHANG);

    if ((ret_sig == SIGCHLD) && (old_sig.sa_handler != SIG_DFL) && (old_sig.sa_handler != SIG_IGN))
      old_sig.sa_handler(ret);

    if (ret == 0) {
      if (ns <= 0)
        return false;
    }
  } while ((ret == 0) ||
          (((ret == -1) && errno == EINTR) ||
           ((ret != -1) && !WIFEXITED(status) && !WIFSIGNALED(status))));

  return true;
}

void Process::closeProcessHandle() noexcept {
  if (pid_ > 0) {
    pid_ = -1;
  }
}

} // namespace process_lib
