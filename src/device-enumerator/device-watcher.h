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
#ifdef _WIN32
#include "usb-watch-win.h"
#else
#include "usb-watch-netlink.h"
#endif

namespace device_enumerator {

#ifdef _WIN32
using DeviceWatcher = UsbWatcherWindows;
#else
using DeviceWatcher = UsbWatcherNetLink;
#endif

class WatchThread : public DeviceWatcher {
  std::thread thread_;
public:
  WatchThread() = default;

  ~WatchThread() {
    stopWatch();
    join();
  }

  template <class Func>
  void startWatch(Func &&f) {
    if (thread_.joinable()) {
      std::terminate();
    }
    thread_ = std::thread([this, func = std::forward<Func>(f)] {
      createWatch(func);
    });
  }

  bool startWatchWaitResult() {
    std::condition_variable cond;
    std::mutex mut;
    constexpr int CREATE_SUCCESS = 2;
    constexpr int CREATE_FAILED = 1;

    int create_ret{0};

    startWatch([&](bool ret) {
      std::lock_guard lk(mut);
      create_ret = ret ? CREATE_SUCCESS : CREATE_FAILED;
      cond.notify_one();
    });

    {
      std::unique_lock lk(mut);
      cond.wait(lk, [&] {
        return create_ret != 0;
      });

      if (create_ret == CREATE_FAILED) {
        return false;
      }
    }

    return true;
  }
 
  void stopWatch() noexcept {
    if (thread_.joinable()) {
      deleteWatch();
    }
  }

  void join() noexcept {
    if (thread_.joinable()) {
      thread_.join();
    }
  }

  struct WatchStopper {
    void operator()(WatchThread* ptr) const {
      ptr->stopWatch();
      ptr->join();
      delete ptr;
    }
  };

  template <class FN>
  [[nodiscard]] static std::unique_ptr<WatchThread, WatchStopper> create(const WatchSettings &settings, FN &&callback) {
    class Impl : public WatchThread {
      FN callback_;
    public:
      Impl(FN&& callback) : callback_(std::forward<FN>(callback)) {}

      void onDeviceInterfaceChanged(const DeviceInterface &dev) override {
        callback_(dev);
      }
    };

    auto watcher = std::unique_ptr<Impl, WatchStopper>(
        new Impl(std::forward<FN>(callback)));

    watcher->initSettings(settings);

    if (!watcher->startWatchWaitResult()) {
      return nullptr;
    }
    
    return watcher;
  }

  template <class FN>
  [[nodiscard]] static auto create(FN &&callback) {
    return create(WatchSettings(), std::forward<FN>(callback));
  }
};

} // namespace device_enumerator
