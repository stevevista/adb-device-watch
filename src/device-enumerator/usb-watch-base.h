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

namespace device_enumerator {

enum class DeviceState : uint32_t {
  None = 0,

  Usb = (1 << 0),
  Net = (1 << 1),

  ComPort = (1 << 2),

  Adb = (1 << 3),
  Fastboot = (1 << 4),
  HDC = (1 << 5),

  Diag = (1 << 6),
  QDL = (1 << 7),

  classWinUsb = Adb | Fastboot | HDC,

  All = static_cast<uint32_t>(-1),
};

template <DeviceState>
struct DeviceStateName;

template<>
struct DeviceStateName<DeviceState::Usb> {
  static constexpr const char *value = "usb";
};

template<>
struct DeviceStateName<DeviceState::Net> {
  static constexpr const char *value = "net";
};

template<>
struct DeviceStateName<DeviceState::ComPort> {
  static constexpr const char *value = "serial";
};

template<>
struct DeviceStateName<DeviceState::Adb> {
  static constexpr const char *value = "adb";
};

template<>
struct DeviceStateName<DeviceState::Fastboot> {
  static constexpr const char *value = "fastboot";
};

template<>
struct DeviceStateName<DeviceState::Diag> {
  static constexpr const char *value = "diag";
};

template<>
struct DeviceStateName<DeviceState::HDC> {
  static constexpr const char *value = "hdc";
};

template<>
struct DeviceStateName<DeviceState::QDL> {
  static constexpr const char *value = "qdloader";
};

constexpr DeviceState operator | (DeviceState a, DeviceState b) {
  return static_cast<DeviceState>(static_cast<uint32_t>(a) | static_cast<uint32_t>(b));
}

constexpr uint32_t operator & (DeviceState a, DeviceState b) {
  return static_cast<uint32_t>(a) & static_cast<uint32_t>(b);
}

constexpr DeviceState operator ~ (DeviceState a) {
  return static_cast<DeviceState>(~static_cast<uint32_t>(a));
}

constexpr DeviceState& operator &= (DeviceState& out, DeviceState a) {
  out  = static_cast<DeviceState>(out & a);
  return out;
}

constexpr DeviceState& operator |= (DeviceState& out, DeviceState a) {
  out  = static_cast<DeviceState>(out | a);
  return out;
}

template <DeviceState S>
constexpr void appendStateIfSet(DeviceState state, std::string& str) {
  if (S & state) {
    if (!str.empty()) str += ",";
    str += DeviceStateName<S>::value;
  }
}

template <DeviceState... States>
constexpr void stringfiyStateT(DeviceState state, std::string& str) {
  (appendStateIfSet<States>(state, str), ...);
}

template <DeviceState... AllStates>
constexpr DeviceState stringToSingleState(std::string_view str) {
    DeviceState result = DeviceState::None;

    (([]<DeviceState S>(std::string_view str, DeviceState& result) {
        if (str == DeviceStateName<S>::value) {
            result = S;
        }
    }.template operator()<AllStates>(str, result)), ...);
    
    return result;
}

template <DeviceState... AllStates>
constexpr DeviceState stringToStateT(std::string_view str) {
  auto tokens = str | std::views::split(',')
            | std::views::transform([](auto&& subrange) {
                std::string_view sv(subrange.begin(), subrange.end());
                auto start = sv.find_first_not_of(" \t\n\r");
                auto end = sv.find_last_not_of(" \t\n\r");
                if (start == std::string_view::npos) return std::string_view{};
                return sv.substr(start, end - start + 1);
            });
    
  auto state = DeviceState::None;
  for (auto t : tokens) {
    state |= stringToSingleState<AllStates...>(t);
  }
  return state;
}

constexpr std::string stringfiyState(DeviceState state) {
  if (state == DeviceState::None)
      return "";

  std::string out;

  stringfiyStateT<
        DeviceState::Usb,
        DeviceState::Net,
        DeviceState::ComPort,
        DeviceState::Adb,
        DeviceState::Fastboot,
        DeviceState::HDC,
        DeviceState::Diag,
        DeviceState::QDL>(state, out);

  return out;
}

constexpr DeviceState stringToState(std::string_view str) {
  return stringToStateT<
        DeviceState::Usb,
        DeviceState::Net,
        DeviceState::ComPort,
        DeviceState::Adb,
        DeviceState::Fastboot,
        DeviceState::HDC,
        DeviceState::Diag,
        DeviceState::QDL>(str);
}

struct DeviceNode {
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

  DeviceState type{DeviceState::None};
  bool off{false};
};

class UsbEnumerator {
public:
  struct WatchSettings {
    std::vector<DeviceState> typeFilters;
    std::vector<uint16_t> includeVids;
    std::vector<uint16_t> excludeVids;
    std::vector<uint16_t> includePids;
    std::vector<uint16_t> excludePids;
    std::vector<std::string> drivers;
    bool enableModprobe{false}; // used in linux to auto loader usb2serial mod
  };

  void initSettings(const WatchSettings &settings);

  virtual ~UsbEnumerator() = default;

protected:
  UsbEnumerator() = default;

  void createAdbTask();
  void deleteAdbTask();
  void initialEnumerateDevices();

  void onUsbInterfaceEnumerated(const std::string &interface_id, DeviceNode&& newdev);
  void onUsbInterfaceOff(const std::string &interface_id);

  virtual void enumerateDevices() = 0;
  virtual void onDeviceInterfaceChanged(const DeviceNode &) {}

protected:
  WatchSettings settings_;
  std::function<void(bool)> initCallback_;

private:
  void onDeviceInterfaceChangedToOff(const std::string &uuid);

  // <serial, uuid>
  std::list<std::pair<std::string, std::string>> adb_serials_;

  struct Trigger { DeviceNode node; int round{0}; };
  task_thread<Trigger> adb_task_;

  std::mutex mutex_;
  // <identity, device>
  std::unordered_map<std::string, DeviceNode> devices_;
};

} // namespace device_enumerator
