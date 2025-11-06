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

#ifdef ENABLE_TEST
#include "../../tests/test_base.h"
#endif
#include "process.h"
#include "string-replace-all.h"
#include <charconv>
#include <ranges>
#include <filesystem>
#ifdef _WIN32
#include "process-win.cc"
#else
#include "process-unix.cc"
#endif

namespace process_lib {

std::filesystem::path
search_exe_path(const std::filesystem::path &exe) {
  return search_exe_path(exe, get_sys_paths());
}

std::filesystem::path
search_exe_path(const std::filesystem::path &exe, const std::vector<std::filesystem::path> &sys_paths) {
  if (exe.is_absolute()) {
    return exe;
  }

#if defined(_WIN32)
  bool has_ext = exe.has_extension();
#endif

  for (auto & sys : sys_paths) {
    auto path = sys / exe;
    std::error_code ec;
    if (std::filesystem::is_regular_file(path, ec)) {
      return path;
    }

#if defined(_WIN32)
    if (!has_ext) {
      {
        auto path = sys / (exe.string() + ".exe");
        std::error_code ec;
        if (std::filesystem::is_regular_file(path, ec)) {
          return path;
        }
      }
      
      {
        auto path = sys / (exe.string() + ".cmd");
        std::error_code ec;
        if (std::filesystem::is_regular_file(path, ec)) {
          return path;
        }
      }

      {
        auto path = sys / (exe.string() + ".bat");
        std::error_code ec;
        if (std::filesystem::is_regular_file(path, ec)) {
          return path;
        }
      }
    }
#endif
  }

  return std::filesystem::path();
}

Process::FileHandle::FileHandle(fd_type fd)
: fd_(fd)
{}

Process::Process(
    std::vector<string_type> &&args, 
    const string_type &workDir,
    ProcessOutputReader &read_stdout,
    ProcessOutputReader &read_stderr,
    uint32_t features,
    bool open_input) noexcept {
  if (open(std::move(args), workDir, true, true, open_input, features)) {
    closed_ = false;

    asyncRead(read_stdout, read_stderr);
  }
}

Process::Process(
    std::vector<string_type> &&args,
    const string_type &workDir,
    std::function<void(const char* bytes, size_t n)> &&read_stdout,
    std::function<void(const char* bytes, size_t n)> &&read_stderr,
    uint32_t features,
    bool detach,
    bool open_input,
    size_t buffer_size) noexcept {

  if (open(std::move(args), workDir, !detach && !!read_stdout, !detach && !!read_stderr, !detach && open_input, features)) {
    closed_ = false;

    if (detach) {
      closeProcessHandle();
    } else {
      asyncRead(std::move(read_stdout), std::move(read_stderr), buffer_size);
    }
  }
}

Process::Process(
    std::vector<string_type> &&args,
    const string_type &workDir,
    uint32_t features,
    bool detach) noexcept
  : Process(
    std::move(args),
    workDir,
    nullptr,
    nullptr,
    features,
    detach,
    0) {}

Process::~Process() noexcept {
  closeFDs();
}

void Process::closeHandles() noexcept {
  {
    std::lock_guard lk(close_mutex);
    closeProcessHandle();
    closed_ = true;
  }

  closeFDs();
}

bool Process::write(const char *bytes, size_t n) noexcept {
  std::lock_guard lock(stdin_mutex);
  if (stdinFd_) {
    return stdinFd_->write(bytes, n);
  }

  return false;
}

void Process::closeFDs() noexcept {
  if(stdout_thread.joinable())
    stdout_thread.join();
  if(stderr_thread.joinable())
    stderr_thread.join();

  {
    std::lock_guard lock(stdin_mutex);
    stdinFd_.reset();
  }

  stdoutFd_.reset();
  stderrFd_.reset();
}

void Process::asyncRead(
    std::function<void(const char* bytes, size_t n)> &&read_stdout,
    std::function<void(const char* bytes, size_t n)> &&read_stderr,
    size_t buffer_size) noexcept {

  if (stdoutFd_) {
    stdout_thread = std::thread([this, buffer_size, read_stdout=std::move(read_stdout)]() mutable {
      size_t n;
      std::unique_ptr<char[]> buffer(new char[buffer_size]);
      for (;;) {
        if (!stdoutFd_->read(buffer.get(), buffer_size, n)) {
          break;
        }

        read_stdout(buffer.get(), n);
      }
    });
  }

  if (stderrFd_) {
    stderr_thread = std::thread([this, buffer_size, read_stderr=std::move(read_stderr)]() mutable {
      size_t n;
      std::unique_ptr<char[]> buffer(new char[buffer_size]);
      for (;;) {
        if(!stderrFd_->read(buffer.get(), buffer_size, n))
          break;
        read_stderr(buffer.get(), n);
      }
    });
  }
}

void Process::asyncRead(
    ProcessOutputReader &read_stdout,
    ProcessOutputReader &read_stderr) noexcept {
  if (stdoutFd_) {
    stdout_thread = std::thread([this, &read_stdout]() {
      size_t buffer_size, n;
      //std::unique_ptr<char[]> buffer(new char[buffer_size]);
      for (;;) {
        auto *ptr = read_stdout.allocateReadBuffer(buffer_size);
        if (!ptr || !buffer_size) {
          break;
        }
        
        if (!stdoutFd_->read(ptr, buffer_size, n)) {
          break;
        }

        read_stdout.commitReadBuffer(n);
      }

      read_stdout.commitReadBuffer(0);
    });
  }

  if (stderrFd_) {
    stderr_thread = std::thread([this, &read_stderr]() {
      size_t buffer_size, n;
      //std::unique_ptr<char[]> buffer(new char[buffer_size]);
      for (;;) {
        auto *ptr = read_stderr.allocateReadBuffer(buffer_size);
        if (!ptr || !buffer_size) {
          break;
        }
        
        if (!stderrFd_->read(ptr, buffer_size, n)) {
          break;
        }

        read_stderr.commitReadBuffer(n);
      }

      read_stderr.commitReadBuffer(0);
    });
  }
}

namespace {

#ifndef WIN32

int GetModuleFileName4(char* sFileName, int nSize) {
  int ret = -1;
  char sLine[1024] = { 0 };
  unsigned long long pSymbol = (unsigned long long)(&GetModuleFileName4);

  FILE *fp = fopen ("/proc/self/maps", "r");
  if ( fp != NULL ) {
    while (!feof (fp)) {
      if ( !fgets (sLine, sizeof (sLine), fp))
        continue;
    
      if ( !strstr (sLine, " r-xp ") || !strchr (sLine, '/'))
        continue;

      // 00406000-00a40000 r-xp 00006000 08:02 7602589 

      unsigned long long start, end;
      sscanf (sLine, "%llx-%llx", &start, &end);

      if (pSymbol >= start && pSymbol < end) {
        char *tmp;
        size_t len;

        /* Extract the filename; it is always an absolute path */
        char *pPath = strchr (sLine, '/');

        /* Get rid of the newline */
        tmp = strrchr (pPath, '\n');
        if (tmp) *tmp = 0;
        ret = 0;
        strcpy( sFileName, pPath );
        break;
      }
    }
    fclose (fp);
  }
  return ret;
}

#endif

std::filesystem::path self_path() noexcept {
#ifdef WIN32
	HMODULE hm = nullptr;

#ifdef _DLL_SOURCE
	GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | 
        GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
        reinterpret_cast<LPCSTR>(&__dummy_addr), &hm);
#endif
	wchar_t wpath[_MAX_PATH];
	DWORD n = GetModuleFileNameW(hm, wpath, _MAX_PATH);
	return std::filesystem::path(wpath);
#else
  char pathbuf[1024] = {0};
  //FILE* stream =fopen("/proc/self/cmdline", "r");
  //char* ret = fgets(pathbuf, 1024, stream);
  //fclose(stream);

  GetModuleFileName4(pathbuf, sizeof(pathbuf));
	return std::filesystem::path(pathbuf);
#endif
}


template <typename T>
std::vector<T> splitCommandTokens(
    const typename T::value_type *script,
    const std::vector<T> &vargs,
    const std::unordered_map<T, T> &kwargs) {
  using CharT = typename T::value_type;
  CharT *d, *t;
  std::vector<T> tokens;
  int argsIndexStep = 0;

  std::vector<CharT> buffer(1024);
  t = d = &buffer[0];
  const auto *d_end = &buffer[buffer.size() - 1];

  int qcount = 0, bcount = 0;
  const auto* s = script;

  while (*s) {
    if ((*s == ' ' || *s == '\t') && qcount == 0) {
      bcount = 0;

      // close the argument
      *d = 0;
      if (d != d_end) d++;

      if (d > t + 1) {
        tokens.push_back(t);
      }
      
      // skip to the next one and initialize it if any
      do {
        s++;
      } while (*s == ' ' || *s == '\t');

      t = d = &buffer[0];

    } else if (*s == '\\') {
      *d = *s++;
      if (d != d_end) d++;

      bcount++;

    } else if (*s=='"') {
      if ((bcount & 1) == 0) {
              /* Preceded by an even number of '\', this is half that
               * number of '\', plus a quote which we erase.
               */
        d -= bcount/2;
        qcount++;
      } else {
              /* Preceded by an odd number of '\', this is half that
               * number of '\' followed by a '"'
               */
        d = d-bcount/2-1;
        *d = '"';
        if (d != d_end) d++;
      }
      s++;
      bcount=0;
          /* Now count the number of consecutive quotes. Note that qcount
           * already takes into account the opening quote if any, as well as
           * the quote that lead us here.
           */
      while (*s == '"') {
        if (++qcount == 3) {
          *d = '"';
          if (d != d_end) d++;
          qcount = 0;
        }
        s++;
      }
      if (qcount == 2)
        qcount = 0;
    } else if (*s == '{') {
      // find closing }
      bcount = 0;
      const auto *p = s + 1;
      size_t len = 0;
      bool alpha = false;
      while (*p && *p != '}') {
        if (!std::isdigit(*p)) {
          alpha = true;
        }
        p++;
        len++;
      }

      bool substracted = false;
      T substract;
      
      if (*p == (CharT)('}')) {
        if (!alpha && len < 5) {
          int argidx;
          if (len == 0) {
            // substract by vary index {}
            argidx = argsIndexStep++;
          } else {
            // substract by {index}
            if constexpr (std::is_same_v<CharT, wchar_t>) {
              argidx = std::wcstol(s + 1, nullptr, 10);
            } else {
              std::from_chars(s + 1, p, argidx);
            }
          }

          if (argidx >= 0 && argidx < (int)vargs.size()) {
            substract = vargs[argidx];
            substracted = true;
          }
        } else {
          // substract by keywords
          auto kwlist = T{s + 1, p};

          auto pos = kwlist.find((CharT)('?'));
          if (pos != T::npos) {
            // {key?t:f}
            auto key = kwlist.substr(0, pos);
            auto it = kwargs.find(key);
            if (it != kwargs.end()) {
              bool is_true;
              if constexpr (std::is_same_v<CharT, wchar_t>) {
                is_true = it->second == L"1" || it->second == L"true";
              } else {
                is_true = it->second == "1" || it->second == "true";
              }
              auto vallist = kwlist.substr(pos + 1);
              pos = vallist.rfind((CharT)(':'));
              if (pos != T::npos) {
                substract = is_true ? vallist.substr(0, pos) : vallist.substr(pos + 1);
              } else {
                if (is_true)
                  substract = vallist;
              }

              substracted = true;
            }
          } else {
            // {key}
            auto it = kwargs.find(kwlist);
            if (it != kwargs.end()) {
              substract = it->second;
              substracted = true;
            } else {
              if constexpr (std::is_same_v<CharT, wchar_t>) {
                if (kwlist == L"arg0") {
                  substract = self_path().c_str();
                }
              } else {
                if (kwlist == "arg0") {
                  substract = self_path().string();
                }
              }
              substracted = true;
            }
          }
        }
      }

      if (substracted) {
        const auto *as = substract.data();
        while (*as) {
          *d = *as++;
          if (d != d_end) d++;  
        }
        s = p + 1;
      } else {
        // regular character
        *d = *s++;
        if (d != d_end) d++;
      }
    } else {
      // regular character
      *d = *s++;
      if (d != d_end) d++;
      bcount = 0;
    }
  }
  *d = '\0';

  if (d > t) {
    tokens.push_back(t);
  }

  return tokens;
}

template <typename T>
int
executeScriptT(
    const typename T::value_type *script,
    const std::vector<T> &vargs,
    const std::unordered_map<T, T> &kwargs,
    long timeoutsMs,
    const T &workDir,
    std::function<void(const char *bytes, size_t n)> &&read_stdout,
    std::function<void(const char *bytes, size_t n)> &&read_stderr,
    uint32_t features,
    bool execute_detach) noexcept {

  auto tokens = splitCommandTokens<T>(script, vargs, kwargs);
  if (tokens.size() && tokens.back().size() == 1 && tokens.back()[0] == '&') {
    tokens.erase(tokens.end() - 1);
    execute_detach = true;
  }

  if (execute_detach) {
    read_stdout = nullptr;
    read_stderr = nullptr;
  }

  Process process(std::move(tokens), workDir, std::move(read_stdout), std::move(read_stderr), features, execute_detach);

  if (execute_detach) {
    return 0;
  }

  int status;
  if (timeoutsMs > 0) {
    if (!process.wait(status, timeoutsMs)) {
      process.kill();
      process.wait(status, 0);
      status = Process::TIMEOUT_ERROR;
    }
  } else {
    status = process.wait();
  }

  return status;
}

} // namespace

int
executeScriptNoOutput(
    const char *script,
    const std::vector<std::string> &vargs,
    const std::unordered_map<std::string, std::string> &kwargs,
    long timeoutsMs,
    const std::string &workDir) noexcept {

  return executeScriptT(
    script,
    vargs,
    kwargs,
    timeoutsMs,
    workDir,
    nullptr,
    nullptr,
    0,
    false);
}

std::tuple<int, std::string, std::string> 
executeScript(
    const char *script,
    const std::vector<std::string> &vargs,
    const std::unordered_map<std::string, std::string> &kwargs,
    long timeoutsMs,
    const std::string &workDir) noexcept {

  std::string out;
  std::string err;

  int status = executeScriptT(
    script,
    vargs,
    kwargs,
    timeoutsMs,
    workDir,
    [&out](const char *bytes, size_t n) {
      out += std::string(bytes, n);
    },
    [&err](const char *bytes, size_t n) {
      err += std::string(bytes, n);
    },
    0,
    false);

  return { status, std::move(out), std::move(err) };
}

void
spawn(
    const char *script,
    const std::vector<std::string> &vargs,
    const std::unordered_map<std::string, std::string> &kwargs,
    const std::string &workDir,
    uint32_t features) noexcept {
  executeScriptT(
    script,
    vargs,
    kwargs,
    0,
    workDir,
    nullptr,
    nullptr,
    features,
    true);
}

#ifdef _WIN32

int
executeScriptNoOutput(
    const wchar_t *script,
    const std::vector<std::wstring> &vargs,
    const std::unordered_map<std::wstring, std::wstring> &kwargs,
    long timeoutsMs,
    const std::wstring &workDir) noexcept {

  return executeScriptT(
    script,
    vargs,
    kwargs,
    timeoutsMs,
    workDir,
    nullptr,
    nullptr,
    0,
    false);
}

std::tuple<int, std::string, std::string> 
executeScript(
    const wchar_t *script,
    const std::vector<std::wstring> &vargs,
    const std::unordered_map<std::wstring, std::wstring> &kwargs,
    long timeoutsMs,
    const std::wstring &workDir) noexcept {

  std::string out;
  std::string err;

  int status = executeScriptT(
    script,
    vargs,
    kwargs,
    timeoutsMs,
    workDir,
    [&out](const char *bytes, size_t n) {
      out += std::string(bytes, n);
    },
    [&err](const char *bytes, size_t n) {
      err += std::string(bytes, n);
    },
    0,
    false);

  return { status, std::move(out), std::move(err) };
}

void
spawn(
    const wchar_t *script,
    const std::vector<std::wstring> &vargs,
    const std::unordered_map<std::wstring, std::wstring> &kwargs,
    const std::wstring &workDir,
    uint32_t features) noexcept {
  executeScriptT(
    script,
    vargs,
    kwargs,
    0,
    workDir,
    nullptr,
    nullptr,
    features,
    true);
}

#endif

std::string executeScriptGetResult(
    const char *script,
    const std::vector<std::string> &vargs,
    const std::unordered_map<std::string, std::string> &kwargs,
    long timeoutsMs,
    const std::string &workDir) noexcept {
  auto [code, output, _] = executeScript(script, vargs, kwargs, timeoutsMs, workDir);
  auto lines = output
        | std::views::split(std::string_view{"\r\n"})
        | std::views::transform([](auto&& range) {
              return std::string(range.begin(), range.end());
          })
        | std::views::filter([](std::string_view sv) {
            return !sv.empty();
        });

  if (lines.begin() == lines.end()) {
    return std::string();
  }
  return *lines.begin();
}

#if __linux__ 

bool runingAsSudoer() {
  auto line = executeScriptGetResult("id -u");
  return std::stoi(line) == 0;
}

#endif

#ifdef ENABLE_TEST

#if __linux__ 
TEST(Process, ScriptGetLine0) {
  auto line = executeScriptGetResult("id -u");
  std::cout << "out: " << line << std::endl;
}
#endif

TEST(Process, SplitTokens0) {
  auto tokens = splitCommandTokens<std::string>("{arg0} puts {{0},{1},{2}}", 
      {"a", "bb", "cc"},
      {});
  std::cout << tokens[0] << std::endl;
  ASSERT_TRUE(tokens.size() == 3);
  ASSERT_TRUE(tokens[2] == "{a,bb,cc}");
}

TEST(Process, SplitTokens1) {
  auto tokens = splitCommandTokens<std::string>("test puts {} {} {} {}", 
      {"a", "bb", "cc"},
      {});
  ASSERT_TRUE(tokens.size() == 6);
  ASSERT_TRUE(tokens[2] == "a");
  ASSERT_TRUE(tokens[3] == "bb");
  ASSERT_TRUE(tokens[4] == "cc");
  ASSERT_TRUE(tokens[5] == "{}");
}

TEST(Process, SplitTokens2) {
  auto tokens = splitCommandTokens<std::string>("test puts {} \"{} {} {}\"", 
      {"a", "bb", "cc"},
      {});
  ASSERT_TRUE(tokens.size() == 4);
  ASSERT_TRUE(tokens[3] == "bb cc {}");
}

TEST(Process, ProcessBlocked) {
  auto code = process::executeScriptNoOutput("hdc list targets");
  std::cout << code << std::endl;
}

TEST(Process, AdbShell01) {
  auto [code, result, err] = process::executeScript("adb shell \"getprop | grep secureboot\"", {});
  std::cout << "code: " << code << std::endl;
  std::cout << "result: " << result << std::endl;
  std::cout << "err: " << err << std::endl;
}

#ifdef _WIN32

TEST(Process, WcharTest) {
  auto tokens = splitCommandTokens<std::wstring>(L"test puts {} {} {} {}", 
      {L"a", L"bb", L"cc"},
      {});
  ASSERT_TRUE(tokens.size() == 6);
  ASSERT_TRUE(tokens[2] == L"a");
  ASSERT_TRUE(tokens[3] == L"bb");
  ASSERT_TRUE(tokens[4] == L"cc");
  ASSERT_TRUE(tokens[5] == L"{}");
}

#endif

#endif // ENABLE_TEST

} // namespace process_lib
