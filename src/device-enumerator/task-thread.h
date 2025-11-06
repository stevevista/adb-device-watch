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

#pragma once

#include <thread>
#include <condition_variable>
#include <mutex>
#include <queue>
#include <chrono>
#include <type_traits>
#include <concepts>
#include <optional>

namespace device_enumerator {

template <class REQ = void>
class task_thread {
  bool stop_requested_{false};
  std::mutex mutex_;
  std::condition_variable condition_;
  std::thread thread_;
  bool consume_all_requests_{false};

  struct None {};

  using QueueType = std::conditional_t<
        !std::is_void_v<REQ>, 
        std::queue<REQ>,
        None>;

  using ReqType = std::conditional_t<
        !std::is_void_v<REQ>, 
        REQ,
        None>;
    
  [[no_unique_address]] QueueType reqs_;

  void stop_impl() {
    {
      std::lock_guard lk(mutex_);
      stop_requested_ = true;
    }
    condition_.notify_all();
        
    if (thread_.joinable()) {
      thread_.join();
    }
  }

  void clear_queue() {
    if constexpr (!std::is_void_v<REQ>) {
      std::lock_guard lk(mutex_);
      while (!reqs_.empty()) {
        reqs_.pop();
      }
    }
  }

public:
  template<typename Function, typename... Args>
  requires std::invocable<Function, REQ&&, Args...> and !std::is_void_v<REQ>
  void start(Function&& f, Args&&... args) {
    if (thread_.joinable()) {
      std::terminate();
    }

    {
      std::lock_guard lk(mutex_);
      stop_requested_ = false;
    }
  
    thread_ = std::thread([this, func = std::forward<Function>(f)] 
                             (auto&&... params) {
      for (;;) {
        std::unique_lock lk(mutex_);
        condition_.wait(lk, [this] {
          return reqs_.size() || stop_requested_;
        });

        if (stop_requested_ && (reqs_.empty() || !consume_all_requests_)) {
          break;
        }

        auto req = std::move(reqs_.front());
        reqs_.pop();

        lk.unlock();

        func(std::move(req), std::forward<decltype(params)>(params)...);
      }

      clear_queue();
    }, std::forward<Args>(args)...);
  }

  template<class Rep, class Period, typename Function, typename... Args>
  requires std::invocable<Function, std::optional<REQ>&&, Args...> and !std::is_void_v<REQ>
  void start(const std::chrono::duration<Rep, Period>& rel_time, Function&& f, Args&&... args) {
    if (thread_.joinable()) {
      std::terminate();
    }

    {
      std::lock_guard lk(mutex_);
      stop_requested_ = false;
    }
  
    thread_ = std::thread([this, rel_time, func = std::forward<Function>(f)] 
                             (auto&&... params) {
      func(std::nullopt, std::forward<decltype(params)>(params)...);

      for (;;) {
        std::optional<REQ> r = std::nullopt;
        std::unique_lock lk(mutex_);
        condition_.wait_for(lk, rel_time, [this] {
          return reqs_.size() || stop_requested_;
        });

        if (stop_requested_ && (reqs_.empty() || !consume_all_requests_)) {
          break;
        }

        if (reqs_.size()) {
          auto req = std::move(reqs_.front());
          reqs_.pop();
          r = std::move(req);
        }

        lk.unlock();

        func(std::move(r), std::forward<decltype(params)>(params)...);
      }

      clear_queue();
    }, std::forward<Args>(args)...);
  }

  template<class Rep, class Period, typename Function, typename... Args>
  requires std::invocable<Function, Args...> and std::is_void_v<REQ>
  void start(const std::chrono::duration<Rep, Period>& rel_time, Function&& f, Args&&... args) {
    if (thread_.joinable()) {
      std::terminate();
    }

    {
      std::lock_guard lk(mutex_);
      stop_requested_ = false;
    }
  
    thread_ = std::thread([this, rel_time, func = std::forward<Function>(f)] 
                             (auto&&... params) {
      func(std::forward<decltype(params)>(params)...);

      for (;;) {
        std::unique_lock lk(mutex_);
        condition_.wait_for(lk, rel_time, [this] {
          return stop_requested_;
        });

        if (stop_requested_) {
          break;
        }

        lk.unlock();

        func(std::forward<decltype(params)>(params)...);
      }

      clear_queue();
    }, std::forward<Args>(args)...);
  }

  task_thread() = default;

  ~task_thread() {
    stop_impl();
  }

  task_thread(const task_thread&) = delete;
  task_thread& operator=(const task_thread&) = delete;
  task_thread(task_thread&&) = delete;
  task_thread& operator=(task_thread&& other) = delete;

  void push_request(ReqType &&req) {
    if constexpr (!std::is_void_v<REQ>) {
      {
        std::lock_guard lk(mutex_);
        reqs_.push(std::move(req));
      }
      condition_.notify_one();
    }
  }

  template <class PRED>
  bool push_request_conditional(ReqType &&req, PRED &&check_dup) {
    if constexpr (!std::is_void_v<REQ>) {
      {
        std::lock_guard lk(mutex_);
        bool duplicated = false;
        auto tmp = std::move(reqs_);
        while (!tmp.empty()) {
          auto r = std::move(tmp.front());
          tmp.pop();
          if (!duplicated) {
            if (check_dup(r)) {
              duplicated = true;
            }
          }
          // push back
          reqs_.push(std::move(r));
        }

        if (duplicated) {
          return false;
        }
        reqs_.push(std::move(req));
      }
      condition_.notify_one();
    }

    return true;
  }

  void stop() {
    stop_impl();
  }

  void set_consume_all_requests(bool consume_all) {
    consume_all_requests_ = consume_all;
  }
};

} // namespace device_enumerator
