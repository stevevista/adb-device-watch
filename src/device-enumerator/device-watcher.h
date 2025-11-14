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
  requires std::invocable<FN, const DeviceInterface &>
  [[nodiscard]] static std::unique_ptr<WatchThread, WatchStopper> create(FN &&callback, const WatchSettings &settings = {}) {
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
};

class WatchWaiter {
  std::unique_ptr<WatchThread, WatchThread::WatchStopper> watcher_;
  std::mutex mutex_;
  DeviceInterface *wait_if_{nullptr};
  std::condition_variable cond_;
  std::unordered_map<std::string, DeviceInterface> ifs_;

  constexpr bool test_match(const DeviceInterface &target, const DeviceInterface &iface) const {
    return (target.off == iface.off) &&
            (target.type == DeviceType::None || (target.type & iface.type)) &&
            (target.devpath.empty() || target.devpath == iface.devpath) &&
            (target.hub.empty() || target.hub == iface.hub) &&
            (target.serial.empty() || target.serial == iface.serial) &&
            (target.ip.empty() || target.ip == iface.ip) &&
            (target.driver.empty() || target.driver == iface.driver) &&
            (target.port == 0 || target.port == iface.port) &&
            (target.vid == 0 || target.vid == iface.vid) &&
            (target.pid == 0 || target.pid == iface.pid) &&
            (target.usbClass == 0 || target.usbClass == iface.usbClass) &&
            (target.usbSubClass == 0 || target.usbSubClass == iface.usbSubClass) &&
            (target.usbProto == 0 || target.usbProto == iface.usbProto) &&
            (target.usbIf < 0 || target.usbIf == iface.usbIf) &&
            (target.identity.empty() || (target.identity == iface.identity || 
                                          target.identity == iface.devpath  || 
                                          target.identity == iface.hub ||
                                          target.identity == iface.serial ||
                                          target.identity == iface.ip ||
                                          target.identity == iface.driver));
  }

  bool match_target(DeviceInterface &target) const {
    for (auto &[id, iface] : ifs_) {
      if (test_match(target, iface)) {
        target = iface;
        return true;
      }
    }
    return false;
  }

public:
  bool start(const WatchThread::WatchSettings &settings = {}) {
    watcher_ = WatchThread::create([this](const DeviceInterface &node) {
      std::unique_lock lock(mutex_);

      ifs_[node.identity] = std::move(node);

      if (wait_if_ != nullptr) {
        if (match_target(*wait_if_)) {
          wait_if_ = nullptr;
          lock.unlock();
          cond_.notify_all();
          lock.lock();
        }
      }
    }, settings);

    return watcher_ != nullptr;
  }

  bool wait_for(DeviceInterface &node, int64_t milliseconds_timeout = -1) noexcept {
    std::unique_lock lock(mutex_);

    if (match_target(node)) {
      return true;
    }

    wait_if_ = &node;

    if (milliseconds_timeout < 0) {
      cond_.wait(lock, [this] {
        return wait_if_ == nullptr;
      });
    } else {
      cond_.wait_for(lock, std::chrono::milliseconds(milliseconds_timeout), [this] {
        return wait_if_ == nullptr;
      });
    }
    if (wait_if_) {
      wait_if_ = nullptr;
      return false;
    }
    return true;
  }

  DeviceInterface wait(DeviceInterface target) noexcept {
    wait_for(target);
    return target;
  }

  bool wait(DeviceInterface target, int64_t milliseconds_timeout, DeviceInterface *out = nullptr) noexcept {
    auto ret = wait_for(target, milliseconds_timeout);
    if (ret && out) {
      *out = std::move(target);
    }
    return ret;
  }

  std::vector<DeviceInterface> get_all(const DeviceInterface *filter) {
    std::vector<DeviceInterface> devices;

    std::lock_guard lock(mutex_);
    for (auto &[id, iface] : ifs_) {
      if (filter == nullptr || test_match(*filter, iface)) {
        devices.push_back(iface);
      }
    }
    return devices;
  }

  void stop() {
    watcher_.reset();
  }
};

} // namespace device_enumerator
