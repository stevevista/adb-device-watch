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
#include "adb-client.h"
#include <algorithm>
#include "picosha2.h"
#include <regex>

namespace device_enumerator {

using namespace adb_client;

namespace {

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

constexpr bool checkTypeFilter(const DeviceNode& node, const UsbEnumerator::WatchSettings& settings) {
  return settings.typeFilters.empty() ||
    std::ranges::any_of(settings.typeFilters, [&node](auto filter) {
      return (node.type & filter) == static_cast<uint32_t>(filter);
    });
}

constexpr bool checkVidFilter(const DeviceNode& node, const UsbEnumerator::WatchSettings& settings) {
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

constexpr bool checkPidFilter(const DeviceNode& node, const UsbEnumerator::WatchSettings& settings) {
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

constexpr bool checkDriverFilter(const DeviceNode& node, const UsbEnumerator::WatchSettings& settings) {
  return settings.drivers.empty() ||
     std::ranges::find(settings.drivers, node.driver) != settings.drivers.end();
}

constexpr bool shouldIncludeDevice(const DeviceNode& node, const UsbEnumerator::WatchSettings& settings) {
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
  createAdbTask();

  enumerateDevices();

  if (initCallback_) {
    std::move(initCallback_)(true);
  }
}

void UsbEnumerator::onUsbInterfaceEnumerated(const std::string &interface_id, DeviceNode&& newdev) {
  if (newdev.vid == QUALCOMM_VID && newdev.pid == QDL_PID) {
    newdev.type |= DeviceState::QDL;
  }

  if (!shouldIncludeDevice(newdev, settings_)) {
    return;
  }

  newdev.identity = createUuid(interface_id);

  {
    std::lock_guard<std::mutex> lock(mutex_);
    devices_[newdev.identity] = newdev;
  }

  if ((newdev.type & (DeviceState::Adb | DeviceState::Usb)) == static_cast<uint32_t>(DeviceState::Adb | DeviceState::Usb)) {
    adb_task_.push_request(Trigger { .node = std::move(newdev) });
    return;
  }

  onDeviceInterfaceChanged(newdev);
}

void UsbEnumerator::onUsbInterfaceOff(const std::string &interface_id) {
  auto uuid = createUuid(interface_id);
  onDeviceInterfaceChangedToOff(uuid);
}

void UsbEnumerator::onDeviceInterfaceChangedToOff(const std::string &uuid) {
  DeviceNode node;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = devices_.find(uuid);
    if (it == devices_.end()) {
      return;
    }

    node = std::move(it->second);
    devices_.erase(it);
  }

  node.off = true;

  if ((node.type & (DeviceState::Adb | DeviceState::Usb)) == static_cast<uint32_t>(DeviceState::Adb | DeviceState::Usb)) {
    adb_task_.push_request(Trigger { .node = node });
    if (node.device.empty() && node.model.empty()) {
      // means not merged with adb devices, no need notify
      return;
    }
  }

  onDeviceInterfaceChanged(node);
}

namespace {

constexpr void merge_adb_info(DeviceNode &dst, DeviceInfo &&src) {
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
        adb_serials_.remove_if([&](auto &d) { return d.second == req->node.identity; });
        req.reset();
      }
    }

    auto devs = adb_list_devices({}, true);

    // check removed devices
    std::erase_if(adb_serials_, [this, &devs](const auto& pair) {
      bool not_found = std::ranges::none_of(devs, [&](const auto& d) { 
        return d.serial == pair.first; 
      });
      
      if (not_found && isRemoteDevice(pair.first)) {
        onDeviceInterfaceChangedToOff(pair.second);
      }
      
      return not_found;
    });

    // check added devices
    for (auto &dev : devs) {
      if (std::ranges::none_of(adb_serials_, [&](auto &d) { return d.first == dev.serial; })) {
        DeviceNode rmote;
        if (isRemoteDevice(dev.serial, &rmote.ip, &rmote.port)) {
          rmote.identity = createUuid(dev.serial);
          rmote.serial = dev.serial;
          rmote.type = DeviceState::Adb | DeviceState::Net;
          merge_adb_info(rmote, std::move(dev));
          adb_serials_.push_back(std::make_pair(dev.serial, rmote.identity));

          if (shouldIncludeDevice(rmote, settings_)) {
            {
              std::lock_guard<std::mutex> lock(mutex_);
              devices_[rmote.identity] = rmote;
            }

            onDeviceInterfaceChanged(rmote);
          }
        } else {
          if (req.has_value()) {
            if (req->node.serial == dev.serial || req->node.serial.empty()) {
              req->node.serial = dev.serial;
              merge_adb_info(req->node, std::move(dev));
              adb_serials_.push_back(std::make_pair(dev.serial, req->node.identity));
              {
                std::lock_guard<std::mutex> lock(mutex_);
                devices_[req->node.identity] = req->node;
              }
              onDeviceInterfaceChanged(req->node);
              req.reset();
            }
          }
        }
      }
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
