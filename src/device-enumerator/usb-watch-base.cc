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

#include "usb-watch-base.h"
#include "adb-client/adb-client.h"
#include <algorithm>
#include "picosha2.h"
#include <regex>

namespace device_enumerator {

using namespace adb_client;

namespace {

constexpr uint8_t ADB_CLASS = 0xff;
constexpr uint8_t ADB_SUBCLASS = 0x42;
constexpr uint8_t ADB_PROTOCOL = 0x01;
constexpr uint8_t FASTBOOT_PROTOCOL = 0x03;
constexpr uint8_t HDC_SUBCLASS = 0x50;
constexpr uint8_t HDC_PROTOCOL = 0x01;

constexpr uint16_t QUALCOMM_VID = 0x05C6;
constexpr uint16_t QDL_PID = 0x9008;
constexpr int MAX_ADB_RETRY_COUNT = 60;
constexpr auto ADB_POLL_INTERVAL = std::chrono::milliseconds(3000);

const std::regex re_remote(R"((\d{1,3}\.\d{1,3}\.\d{1,3}\.\d{1,3}):(\d{1,5}))");

std::string createUuid(std::string_view interface_id) {
  std::vector<unsigned char> hash(picosha2::k_digest_size);
  picosha2::hash256(interface_id.begin(), interface_id.end(), hash.begin(), hash.end());
  return picosha2::bytes_to_hex_string(hash.begin(), hash.begin() + 16);
}

bool isRemoteDevice(const std::string &serial, std::string *ip = nullptr, uint16_t *port = nullptr) noexcept {
  try {
    std::smatch matches;
    if (!std::regex_match(serial, matches, re_remote) || matches.size() != 3) {
      return false;
    }

    if (ip) {
      *ip = matches[1];
    }

    if (port) {
      *port = std::stoi(matches[2].str());
    }
    return true;
  } catch (const std::regex_error& e) {
    return false;
  }
}

constexpr bool checkTypeFilter(const DeviceInterface& node, const UsbEnumerator::WatchSettings& settings) {
  return settings.typeFilters.empty() ||
    std::ranges::any_of(settings.typeFilters, [&node](auto filter) {
      return (node.type & filter) == static_cast<uint32_t>(filter);
    });
}

constexpr bool checkVidFilter(const DeviceInterface& node, const UsbEnumerator::WatchSettings& settings) {
  if (settings.includeVids.size() &&
      std::ranges::find(settings.includeVids, node.vid) == settings.includeVids.end()) {
    return false;
  }

  if (settings.excludeVids.size() &&
      node.vid &&
      std::ranges::find(settings.excludeVids, node.vid) != settings.excludeVids.end()) {
    return false;
  }

  return true;
}

constexpr bool checkPidFilter(const DeviceInterface& node, const UsbEnumerator::WatchSettings& settings) {
  if (settings.includePids.size() &&
      std::ranges::find(settings.includePids, node.pid) == settings.includePids.end()) {
    return false;
  }

  if (settings.excludePids.size() &&
      node.pid &&
      std::ranges::find(settings.excludePids, node.pid) != settings.excludePids.end()) {
    return false;
  }

  return true;
}

constexpr bool checkDriverFilter(const DeviceInterface& node, const UsbEnumerator::WatchSettings& settings) {
  return settings.drivers.empty() ||
     std::ranges::find(settings.drivers, node.driver) != settings.drivers.end();
}

constexpr bool shouldIncludeDevice(const DeviceInterface& node, const UsbEnumerator::WatchSettings& settings) {
  return checkTypeFilter(node, settings) &&
         checkVidFilter(node, settings) &&
         checkPidFilter(node, settings) &&
         checkDriverFilter(node, settings);
}

} // namespace

void UsbEnumerator::initSettings(const WatchSettings &settings) {
  settings_ = settings;
}

void UsbEnumerator::initialEnumerateDevices() {
  if (settings_.enableAdbClient) {
    createAdbTask();
  }

  enumerateDevices();

  if (initCallback_) {
    std::move(initCallback_)(true);
  }
}

void UsbEnumerator::onUsbInterfaceEnumerated(const std::string &interface_id, DeviceInterface&& newdev) {
  if (newdev.type & DeviceType::Usb) {
    if (newdev.usbClass == ADB_CLASS) {
      if (newdev.usbSubClass == HDC_SUBCLASS && newdev.usbProto == HDC_PROTOCOL) {
        newdev.type |= DeviceType::HDC;
      } else if (newdev.usbSubClass == ADB_SUBCLASS && newdev.usbProto == ADB_PROTOCOL) {
        newdev.type |= DeviceType::Adb;
      } else if (newdev.usbSubClass == ADB_SUBCLASS && newdev.usbProto == FASTBOOT_PROTOCOL) {
        newdev.type |= DeviceType::Fastboot;
      }
    }
  }

  if (newdev.vid == QUALCOMM_VID && newdev.pid == QDL_PID) {
    newdev.type |= DeviceType::QDL;
  }

  if (!shouldIncludeDevice(newdev, settings_)) {
    return;
  }

  newdev.identity = createUuid(interface_id);

  if ((newdev.type & DeviceType::usbConnectedAdb) == static_cast<uint32_t>(DeviceType::usbConnectedAdb)) {
    if (settings_.enableAdbClient) {
      // cache the adb device for later use
      {
        std::lock_guard lock(mutex_);
        cached_interfaces_[newdev.identity] = newdev;
      }

      adb_task_.push_request(Trigger { .node = std::move(newdev) });
      return;
    }
  }

  onDeviceInterfaceChangedToOn(newdev);
}

void UsbEnumerator::onUsbInterfaceOff(const std::string &interface_id) {
  auto uuid = createUuid(interface_id);
  
  DeviceInterface node;
  {
    std::lock_guard lock(mutex_);
    auto it = cached_interfaces_.find(uuid);
    if (it == cached_interfaces_.end()) {
      return;
    }

    node = std::move(it->second);
    cached_interfaces_.erase(it);
  }

  node.off = true;

  if ((node.type & DeviceType::usbConnectedAdb) == static_cast<uint32_t>(DeviceType::usbConnectedAdb)) {
    if (settings_.enableAdbClient) {
      adb_task_.push_request(Trigger { .node = node });
      if (node.device.empty() && node.model.empty()) {
        // means not merged with adb devices, no need notify
        return;
      }
    }
  }

  onDeviceInterfaceChanged(node);
}

void UsbEnumerator::onDeviceInterfaceChangedToOn(const DeviceInterface &node) {
  {
    std::lock_guard lock(mutex_);
    cached_interfaces_[node.identity] = node;
  }

  onDeviceInterfaceChanged(node);
}

namespace {

constexpr void merge_adb_info(DeviceInterface &dst, DeviceInfo &&src) {
  dst.serial = std::move(src.serial);
  dst.product = std::move(src.product);
  dst.model = std::move(src.model);
  dst.device = std::move(src.device);
}

} // namespace

void UsbEnumerator::createAdbTask() {
  adb_task_.set_consume_all_requests(true);

  adb_task_.start(ADB_POLL_INTERVAL, [this](std::optional<Trigger> &&req) {
    if (req.has_value()) {
      if (req->node.off) {
        adb_serials_.remove(req->node.serial);
        req.reset();
      }
    }

    std::vector<DeviceInfo> devs;
    try {
      devs = adb_list_devices({}, true);
    } catch (std::exception &e) {
      std::cerr << "adb_list_devices failed: " << e.what() << std::endl;
      adb_task_.notify_stop();
      return;
    }

    // check removed devices
    std::erase_if(adb_serials_, [this, &devs](const auto& serial) {
      bool not_found = std::ranges::none_of(devs, [&serial](const auto& d) { 
        return d.serial == serial; 
      });
      
      if (not_found && isRemoteDevice(serial)) {
        onUsbInterfaceOff(serial);
      }
      
      return not_found;
    });

    // check added devices
    std::vector<DeviceInfo> newly_added;

    for (auto &dev : devs) {
      if (std::ranges::none_of(adb_serials_, [&](auto &serial) { return serial == dev.serial; })) {
        DeviceInterface rmote;
        if (isRemoteDevice(dev.serial, &rmote.ip, &rmote.port)) {
          rmote.identity = createUuid(dev.serial);
          rmote.type = DeviceType::remoteAdb;
          merge_adb_info(rmote, std::move(dev));
          adb_serials_.push_back(rmote.serial);

          if (shouldIncludeDevice(rmote, settings_)) {
            onDeviceInterfaceChangedToOn(rmote);
          }
        } else {
          if (req.has_value()) {
            if (req->node.serial == dev.serial || req->node.serial.empty()) {
              if (req->node.serial == dev.serial) {
                // matched by serial exactlly
                // make this dev sort at head
                dev.transportId = -1;
              }

              newly_added.push_back(std::move(dev));
            }
          }
        }
      }
    }

    if (newly_added.size()) {
      std::ranges::sort(newly_added, [](const auto& a, const auto& b) {
        return a.transportId < b.transportId;
      });

      merge_adb_info(req->node, std::move(newly_added[0]));
      adb_serials_.push_back(req->node.serial);

      onDeviceInterfaceChangedToOn(req->node);
      req.reset();
    }

    if (req.has_value() && req->round < MAX_ADB_RETRY_COUNT) {
      auto identity = req->node.identity;
      req->round++;
      bool added = adb_task_.push_request_conditional(std::move(*req), [&identity](auto &r) {
        return r.node.identity == identity;
      });

      if (added) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
      }
    }
  });
}

void UsbEnumerator::deleteAdbTask() {
  adb_task_.stop();
}

} // namespace device_enumerator
