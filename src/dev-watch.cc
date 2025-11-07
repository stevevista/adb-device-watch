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
#include "adb-client.h"
#include <gflags/gflags.h>
#include <mutex>
#include <thread>
#include <condition_variable>
#include <iostream>
#include <codecvt>
#include <nlohmann/json.hpp>

using namespace adb_client;

using json = nlohmann::json;

using device_enumerator::DeviceNode;
using device_enumerator::WatchThread;

namespace nlohmann {
  template <>
  struct adl_serializer<std::wstring> {
    static void to_json(json& j, const std::wstring& str) {
      std::wstring_convert<std::codecvt_utf8<wchar_t>> converter;
      j = converter.to_bytes(str);
    }
  };
}


DEFINE_bool(pretty, false,
                  "pretty json output.");

DEFINE_bool(watch, false,
                  "watch devices changes.");

#if __linux__ 
DEFINE_bool(usbserial_vidpid, "",
                  "auto load usbserial driver for specific vid:pid. e.g. 2341:2341,1234,5678");
#endif

DEFINE_string(usbserial_vidpid, "",
                  "auto load usbserial driver for specific vid:pid. e.g. 2341:2341,1234,5678");

// the following flags control device watcch
         
DEFINE_string(vids, "",
                  "usb vid include/exclude list. e.g. 2341,!1234"); 

DEFINE_string(pids, "",
                  "usb pid include/exclude list. e.g. 2341,!1234"); 

DEFINE_string(types, "",
                  "combination of device type. e.g. usb,adb|net");

DEFINE_string(drivers, "",
                  "drivers filter. e.g. qcserial,WinUSB");
  
DEFINE_string(ip_list, "",
                  "watch ip list");

namespace {

json 
deviceNodeToJsonObject(const DeviceNode &dev) {
  json jdev;
  
  jdev["id"] = dev.identity;
  if (dev.off) jdev["off"] = dev.off;
  if (!dev.devpath.empty()) jdev["devpath"] = dev.devpath;
  if (!dev.hub.empty()) jdev["hub"] = dev.hub;
  if (!dev.serial.empty()) jdev["serial"] = dev.serial;
  if (!dev.manufacturer.empty()) jdev["manufacturer"] = dev.manufacturer;
  if (!dev.product.empty()) jdev["product"] = dev.product;
  if (!dev.model.empty()) jdev["model"] = dev.model;
  if (!dev.device.empty()) jdev["device"] = dev.device;
  if (!dev.driver.empty()) jdev["driver"] = dev.driver;
  if (!dev.ip.empty()) jdev["ip"] = dev.ip;
  if (dev.port) jdev["port"] = dev.port;
  if (dev.vid) jdev["vid"] = dev.vid;
  if (dev.pid) jdev["pid"] = dev.pid;
  jdev["type"] = device_enumerator::DeviceTypeConverter::stringfiyType(dev.type);
  if (!dev.description.empty()) jdev["description"] = dev.description;
  if (dev.hasUsbClass) {
    jdev["usbClass"] = dev.usbClass;
    jdev["usbSubClass"] = dev.usbSubClass;
    jdev["usbProto"] = dev.usbProto;
  }

  return jdev;
}

template <class T>
requires std::is_integral_v<T>
constexpr bool to_integral(const char* first, const char* last, T &value) {
  int base = 10;

  if (first[0] == '0' && (first[1] == 'x' || first[1] == 'X')) {
    base = 16;
    first += 2;
  }
  auto [ptr, ec] = std::from_chars(first, last, value, base);
  return ec == std::errc();
}


constexpr void parse_id_list(std::vector<uint16_t> &includes, std::vector<uint16_t> &excludes, const std::string & arg) {
  auto list = arg 
            | std::views::split(',')
            | std::views::transform([](auto&& subrange) -> std::string_view {
                return std::string_view(subrange.begin(), subrange.end());
            })
            | std::views::filter([](std::string_view s) {
                return !s.empty();
            });

  for (auto str : list) {
    uint16_t value = 0;
    int base = 10;
    auto start = str.data();
    auto end = start + str.size();
    bool exclude = false;
    if (start[0] == '!') {
      exclude = true;
      start += 1;
    }

    to_integral(start, end, value);

    if (exclude) {
      excludes.push_back(value);
    } else {
      includes.push_back(value);
    }
  }
}

} // namespace


int main(int argc, char *argv[]) {
#ifdef _WIN32
  SetThreadUILanguage(MAKELANGID(LANG_ENGLISH, SUBLANG_ENGLISH_US));
#endif

#if __linux__ 
  if (FLAGS_usbserial_vidpid.size() && !process::runingAsSudoer()) {
    std::cerr << "require sudo privileges. " << std::endl;
    return 1;
  }
#endif

  gflags::SetVersionString(VERSION);
  gflags::SetUsageMessage(
    "Sample usage:\n adb-device-watch --pretty\n"
    "--types=\"usb,adb|serial\"\n"
    "--vids=\"0x124,!0x123\"\n");

  gflags::ParseCommandLineFlags(&argc, &argv, true);

  WatchThread::WatchSettings settings;

#if __linux__ 
  auto vidpid_list = FLAGS_usbserial_vidpid
            | std::views::split(',')
            | std::views::transform([](auto&& subrange) -> std::string_view {
                return std::string_view(subrange.begin(), subrange.end());
            })
            | std::views::filter([](std::string_view s) {
                return !s.empty();
            });
  
  for (auto vidpid : vidpid_list) {
    auto pos = vidpid.find(':');
    if (pos == std::string_view::npos) {
      std::cerr << "invalid vid:pid format: " << vidpid << std::endl;
      return 1;
    }
    uint16_t vid = 0, pid = 0;
    to_integral(vidpid.data(), vidpid.data() + pos, vid);
    to_integral(vidpid.data() + pos + 1, vidpid.data() + vidpid.size(), pid);
    settings.usb2serialVidPid.push_back({vid, pid});
  }
#endif

  parse_id_list(settings.includeVids, settings.excludeVids, FLAGS_vids);
  parse_id_list(settings.includePids, settings.excludePids, FLAGS_pids);

  auto filters = FLAGS_types | std::views::split('|')
              | std::views::transform([](auto&& subrange) {
                return std::string_view(subrange.begin(), subrange.end());
              })
              | std::views::filter([](std::string_view s) {
                return !s.empty();
            });

  for (auto filter : filters) {
    settings.typeFilters.push_back(device_enumerator::DeviceTypeConverter::stringToType(filter));
  }

  auto drivers = FLAGS_drivers
            | std::views::split(',')
            | std::views::transform([](auto&& subrange) -> std::string_view {
                return std::string_view(subrange.begin(), subrange.end());
            })
            | std::views::filter([](std::string_view s) {
                return !s.empty();
            });

  for (auto driver : drivers) {
    settings.drivers.push_back(std::string(driver));
  }

  auto ip_list = FLAGS_ip_list 
            | std::views::split(',')
            | std::views::transform([](auto&& subrange) -> std::string_view {
                return std::string_view(subrange.begin(), subrange.end());
            })
            | std::views::filter([](std::string_view s) {
                return !s.empty();
            })
            | std::views::transform([](std::string_view str) -> std::string {
              return std::string("connect:") + std::string(str);
            });

  for (auto ip : ip_list) {
    try {
      adb_command_query(ip);
    } catch (const std::exception& e) {
      // std::cerr << "connect IP failed: " << e.what() << std::endl;
    }
  }

  auto watcher = WatchThread::create(settings, [](const DeviceNode &dev) {
    auto jdev = deviceNodeToJsonObject(dev);
    std::cout << jdev.dump(FLAGS_pretty ? 4 : -1) << std::endl;
  });

  if (!watcher) {
    std::cerr << "create watcher failed." << std::endl;
    return 1;
  }

  if (FLAGS_watch) {
    getchar();
  }

  return 0;
}
