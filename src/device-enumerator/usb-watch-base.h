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
#include "task-thread.h"
#include <string>
#include <vector>
#include <tuple>
#include <functional>
#include <mutex>
#include <unordered_map>
#include <iostream>
#include <ranges>
#include <list>
#include <array>

namespace device_enumerator {

enum class DeviceType : uint32_t {
  None = 0,

  Usb = (1 << 0),
  Net = (1 << 1),

  Serial = (1 << 2),

  Adb = (1 << 3),
  Fastboot = (1 << 4),
  HDC = (1 << 5),

  Diag = (1 << 6),
  QDL = (1 << 7),

  usbConnectedAdb = Adb | Usb,
  remoteAdb = Adb | Net,

  All = static_cast<uint32_t>(-1),
};

constexpr DeviceType operator | (DeviceType a, DeviceType b) {
  return static_cast<DeviceType>(static_cast<uint32_t>(a) | static_cast<uint32_t>(b));
}

constexpr uint32_t operator & (DeviceType a, DeviceType b) {
  return static_cast<uint32_t>(a) & static_cast<uint32_t>(b);
}

constexpr DeviceType operator ~ (DeviceType a) {
  return static_cast<DeviceType>(~static_cast<uint32_t>(a));
}

constexpr DeviceType& operator &= (DeviceType& out, DeviceType a) {
  out  = static_cast<DeviceType>(out & a);
  return out;
}

constexpr DeviceType& operator |= (DeviceType& out, DeviceType a) {
  out  = static_cast<DeviceType>(out | a);
  return out;
}

class DeviceTypeConverter {
  static constexpr std::array kSupportedTypes = {
        std::pair{DeviceType::Usb, "usb"},
        std::pair{DeviceType::Net, "net"},
        std::pair{DeviceType::Serial, "serial"},
        std::pair{DeviceType::Adb, "adb"},
        std::pair{DeviceType::Fastboot, "fastboot"},
        std::pair{DeviceType::HDC, "hdc"},
        std::pair{DeviceType::Diag, "diag"},
        std::pair{DeviceType::QDL, "qdl"}
    };

public:
  static constexpr std::string stringfiyType(DeviceType state) {
    std::string str;

    if (state == DeviceType::None)
      return str;
        
    for (const auto& [type, name] : kSupportedTypes) {
      if (state & type) {
        if (!str.empty()) str += ",";
        str += name;
      }
    }
    return str;
  }

  static constexpr DeviceType stringToType(std::string_view str) {
    auto tokens = str | std::views::split(',')
            | std::views::transform([](auto&& subrange) {
                std::string_view sv(subrange.begin(), subrange.end());
                auto start = sv.find_first_not_of(" \t\n\r");
                auto end = sv.find_last_not_of(" \t\n\r");
                if (start == std::string_view::npos) return std::string_view{};
                return sv.substr(start, end - start + 1);
            });
    
    auto state = DeviceType::None;
    for (auto t : tokens) {
      for (const auto& [type, name] : kSupportedTypes) {
        if (t == name) {
          state |= type;
          break;
        }
      }
    }
    return state;
  }
};

struct DeviceInterface {
  std::string identity;

  std::string devpath;
  std::string hub;
  std::string serial;
  std::string manufacturer;
  std::string product;
  std::string model;
  std::string device;
  std::string ip;
  uint16_t port{0};
  std::string driver;

#ifdef _WIN32
  std::wstring
#else
  std::string 
#endif
            description;
  uint16_t vid{0};
  uint16_t pid{0};
  // UsbSpeed speed;

  uint8_t usbClass{0};
  uint8_t usbSubClass{0};
  uint8_t usbProto{0};
  int usbIf{-1}; // >= 0 means composite deviced

  DeviceType type{DeviceType::None};
  bool off{false};
};

struct CompositeDevice {
  std::string identity; // usbid, comport, or IP
  std::vector<DeviceInterface> interfaces;
  DeviceType type{DeviceType::None};
};

class UsbEnumerator {
public:
  struct WatchSettings {
    bool enableAdbClient{true};
    bool enableCompositeDevice{false};
    std::vector<DeviceType> typeFilters;
    std::vector<uint16_t> includeVids;
    std::vector<uint16_t> excludeVids;
    std::vector<uint16_t> includePids;
    std::vector<uint16_t> excludePids;
    std::vector<std::string> drivers;
#if __linux__ 
    std::vector<std::pair<uint16_t, uint16_t>> usb2serialVidPid;
#endif
  };

  void initSettings(const WatchSettings &settings);

  virtual ~UsbEnumerator() = default;

protected:
  UsbEnumerator() = default;

  void deleteAdbTask();
  void initialEnumerateDevices();

  void onUsbInterfaceEnumerated(const std::string &interface_id, DeviceInterface&& newdev);
  void onUsbInterfaceOff(const std::string &interface_id);

 private: 
  virtual void enumerateDevices() = 0;
  virtual void onDeviceInterfaceChanged(const DeviceInterface &) {}
  virtual void onCompositeDeviceChanged(const CompositeDevice &, const DeviceInterface &) {}

  void createAdbTask();
  void onDeviceInterfaceChangedWrap(const DeviceInterface &);
  void onDeviceInterfaceChangedToOff(const std::string &uuid);

protected:
  WatchSettings settings_;
  std::function<void(bool)> initCallback_;

private:
  // <serial, uuid>
  std::list<std::pair<std::string, std::string>> adb_serials_;

  struct Trigger { DeviceInterface node; int round{0}; };
  task_thread<Trigger> adb_task_;

  std::mutex mutex_;
  // <identity, device>
  std::unordered_map<std::string, DeviceInterface> cached_interfaces_;
  std::unordered_map<std::string, CompositeDevice> cached_composite_devices_;
};

} // namespace device_enumerator
