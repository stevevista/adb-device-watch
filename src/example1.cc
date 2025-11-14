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

#include "device-enumerator/device-watcher.h"
#include "device-enumerator/shorthash.h"
#include <mutex>
#include <vector>

using device_enumerator::DeviceInterface;
using device_enumerator::WatchThread;
using device_enumerator::DeviceType;

constexpr uint32_t FLAGS_EDL = (1 << 0);
constexpr uint32_t FLAGS_TL = (1 << 1);
constexpr uint32_t FLAGS_EDL_TL_maybe = (1 << 2);
constexpr uint32_t FLAGS_ADB = (1 << 3);
constexpr uint32_t FLAGS_FASTBOOT = (1 << 4);
constexpr uint32_t FLAGS_DIAG = (1 << 5);
constexpr uint32_t FLAGS_UART = (1 << 6);

class UsbWatcher {
public:
  struct Device {
    std::string id;
    std::vector<DeviceInterface> nodes;
    uint32_t flags{0};
  };

private:
  std::unique_ptr<WatchThread, WatchThread::WatchStopper> watcher_;
  mutable std::mutex mutex_;
  std::unordered_map<std::string, Device> devices_;

  std::function<void(const Device&, uint32_t)> callback_;

  const char *waitId_{nullptr};
  uint32_t waitFlags_{0};
  std::condition_variable cond_;
  Device *waitedDev_{nullptr};

  std::vector<Device> queryDevicesNoLock(const char *target, uint32_t flags) const {
    std::vector<Device> result;
    for (const auto& [id, dev] : devices_) {
      if ((target == nullptr || id == target) && (dev.flags & flags)) {
        result.push_back(dev);
      }
    }
    return result;
  }

  void on_node_event(const DeviceInterface &node) {
    std::string id;
    if (node.type & DeviceType::Usb) {
      id = node.hub;
    } else if (node.type & DeviceType::Adb) {
      id = node.serial;
    } else if (node.type & DeviceType::Serial) {
      id = node.devpath;
    } else {
      id = node.identity;
    }

    Device dev;
    uint32_t flags = 0;

    if (!node.off) {
      if (node.driver == "JLQUSBSerDL" || 
          node.driver == "JLQ_DOWNLOAD_SERVICES" ||
          node.driver == "JLQ_DOWNLOAD_SERVICE") {
        if (node.description.find(L"DOWNLOAD BOOTROM") != std::wstring::npos) {
          flags = FLAGS_EDL;
        } else if (node.description.find(L"DOWNLOAD TL") != std::wstring::npos) {
          flags = FLAGS_TL;
        } else {
          flags = FLAGS_EDL_TL_maybe;
        }
      } else if ((node.type & DeviceType::usbConnectedAdb) == static_cast<uint32_t>(DeviceType::usbConnectedAdb)) {
        flags = FLAGS_ADB;
      } else if (node.type & DeviceType::Fastboot) {
        flags = FLAGS_FASTBOOT;
      } else if (node.type & DeviceType::Diag) {
        flags = FLAGS_DIAG;
      } else if (node.type & DeviceType::Serial) {
        flags = FLAGS_UART;
      }
    }

    std::unique_lock lock(mutex_);

    if (node.off) {
      auto it = devices_.find(id);
      if (it == devices_.end()) {
        return;
      }
      dev = std::move(it->second);
      devices_.erase(it);
    } else {
      auto it = devices_.find(id);
      if (it == devices_.end()) {
        devices_.emplace(id, Device{id, {node}});
        it = devices_.find(id);
      } else {
        it->second.nodes.push_back(node);
      }

      it->second.flags |= flags;
      dev = it->second;
    }

    auto cb = callback_;

    if (waitedDev_ && ((waitFlags_ & flags) || (waitFlags_ == flags))) {
      if ((waitId_ == nullptr || waitId_ == id)) {
        *waitedDev_ = dev;
        waitId_ = nullptr;
        waitedDev_ = nullptr;
        lock.unlock();
        cond_.notify_all();
        lock.lock();
      }
    }

    lock.unlock();
    if (cb) cb(dev, flags);
  }

public:
  void set_callback(std::function<void(const Device&, uint32_t)> &&cb) {
    std::unique_lock lk(mutex_);
    callback_ = std::move(cb);
  }

  void start() {
    WatchThread::WatchSettings settings;

    watcher_ = WatchThread::create([this](const DeviceInterface &node) {
      on_node_event(node);
    }, settings);
  }

  bool waitFor(const char *target, uint32_t flags, int64_t milliseconds_timeout, Device &dev) noexcept {
    std::unique_lock lk(mutex_);

    if (flags != 0 || milliseconds_timeout == 0) {
      auto devices = queryDevicesNoLock(target, flags);
      if (devices.size()) {
        dev = std::move(devices[0]);
        return true;
      }
    }

    if (milliseconds_timeout == 0) {
      return false;
    }

    waitedDev_ = &dev;
    waitId_ = target;
    waitFlags_ = flags;

    if (milliseconds_timeout < 0) {
      cond_.wait(lk, [this] { return waitedDev_ == nullptr; });
    } else {
      cond_.wait_for(lk, std::chrono::milliseconds(milliseconds_timeout), [this] { return waitedDev_ == nullptr; });
    }

    if (waitedDev_ != nullptr) {
      waitId_ = nullptr;
      waitedDev_ = nullptr;
      return false;
    }

    return true;
  }

  Device wait(const char *target, uint32_t flags) noexcept {
    Device dev;
    waitFor(target, flags, -1, dev);
    return dev;
  }

  bool waitForOff(const char *target, int64_t milliseconds_timeout = -1) noexcept {
    Device dev;
    return waitFor(target, 0, milliseconds_timeout, dev);
  }

  std::vector<Device> queryDevices(const char *target, uint32_t flags) const {
    std::lock_guard lk(mutex_);
    return queryDevicesNoLock(target, flags);
  }
};

int main() {
  /*
  UsbWatcher watcher;
  watcher.set_callback([](const auto& dev, uint32_t flags) {
    std::cout << "device " << dev.id << " flags: " << dev.flags << std::endl;
  });
  watcher.start();

  UsbWatcher::Device dev;
  watcher.waitForOff(nullptr, -1);

  std::cout << "device off: " << dev.id << " flags: " << dev.flags << std::endl;
  */

  device_enumerator::WatchWaiter waiter;
  device_enumerator::DeviceInterface dev;
  dev.type = DeviceType::Adb;
  WatchThread::WatchSettings settings;
  waiter.start(settings);
  waiter.wait_for(dev);
  std::cout << "device: " << dev.identity << std::endl;

  getchar();
  return 0;
}
