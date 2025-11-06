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

const std::regex re_remote(R"((\d{1,3}\.\d{1,3}\.\d{1,3}\.\d{1,3}):(\d{1,5}))");

std::string createUuid(const std::string &interface_id) {
  std::vector<unsigned char> hash(picosha2::k_digest_size);
  picosha2::hash256(interface_id.begin(), interface_id.end(), hash.begin(), hash.end());
  return picosha2::bytes_to_hex_string(hash.begin(), hash.begin() + 16);
}

bool isRemoteDevice(const std::string &serial, std::string *ip = nullptr, uint16_t *port = nullptr) {
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

void UsbEnumerator::onInterfaceEnumerated(const std::string &interface_id, DeviceNode&& newdev) {
  if (newdev.vid == QUALCOMM_VID) {
    if (newdev.pid == 0x9008) {
      newdev.type |= DeviceState::QDL;
    }
  }

  if (settings_.typeFilters.size()) {
    bool type_match = false;
    for (auto filter : settings_.typeFilters) {
      if ((newdev.type & filter) == static_cast<uint32_t>(filter)) {
        type_match = true;
        break;
      }
    }
    if (!type_match) {
      return;
    }
  }

  if (settings_.excludeVids.size()) {
    if (newdev.vid && std::ranges::find(settings_.excludeVids, newdev.vid) != settings_.excludeVids.end()) {
      return;
    }
  }

  if (settings_.includeVids.size()) {
    if (!newdev.vid || std::ranges::find(settings_.includeVids, newdev.vid) == settings_.includeVids.end()) {
      return;
    }
  }

  if (settings_.excludePids.size()) {
    if (newdev.pid && std::ranges::find(settings_.excludePids, newdev.pid) != settings_.excludePids.end()) {
      return;
    }
  }

  if (settings_.drivers.size()) {
    if (std::ranges::find(settings_.drivers, newdev.driver) == settings_.drivers.end()) {
      return;
    }
  }

  if (settings_.includePids.size()) {
    if (!newdev.pid || std::ranges::find(settings_.includePids, newdev.pid) == settings_.includePids.end()) {
      return;
    }
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

  onDeviceInterfaceChanged(std::move(newdev));
}

void UsbEnumerator::onInterfaceOff(const std::string &interface_id) {
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
    node.off = true;
    devices_.erase(it);
  }

  if ((node.type & (DeviceState::Adb | DeviceState::Usb)) == static_cast<uint32_t>(DeviceState::Adb | DeviceState::Usb)) {
    adb_task_.push_request(Trigger { .node = node });
  }

  onDeviceInterfaceChanged(std::move(node));
}

namespace {

constexpr void merge_adb_info(DeviceNode &dst, DeviceInfo &&src) {
  dst.product = std::move(src.product);
  dst.model = std::move(src.model);
  dst.device = std::move(src.device);
}

}

void UsbEnumerator::createAdbTask() {
  adb_task_.set_consume_all_requests(true);
  adb_task_.start(std::chrono::seconds(3000), [this](std::optional<Trigger> &&r) {
    int round = 0;
    bool check_up = false;
    DeviceNode node;
    if (r.has_value()) {
      node = std::move(r->node);
      check_up = !node.off;
      round = r->round;

      if (node.off) {
        adb_serials_.remove_if([&](auto &d) { return d.second == node.identity; });
      }
    }

    auto devs = adb_list_devices({}, true);

    // check removed devices
    auto it = adb_serials_.begin();
    while (it != adb_serials_.end()) {
        auto itor = it;
        it++;

        if (devs.end() == std::ranges::find_if(devs, [&](auto &d) { return d.serial == itor->first; })) {
          if (isRemoteDevice(itor->first)) {
            onDeviceInterfaceChangedToOff(itor->second);
          }

          adb_serials_.erase(itor);
        }
    }

    // check added devices
    for (auto &dev : devs) {
        if (adb_serials_.end() == std::ranges::find_if(adb_serials_, [&](auto &d) { return d.first == dev.serial; })) {
          DeviceNode rmote;
          if (isRemoteDevice(dev.serial, &rmote.ip, &rmote.port)) {
            rmote.identity = createUuid(dev.serial);
            rmote.serial = dev.serial;
            rmote.type = DeviceState::Adb | DeviceState::Net;
            merge_adb_info(rmote, std::move(dev));
            adb_serials_.push_back(std::make_pair(dev.serial, rmote.identity));
            onInterfaceEnumerated(dev.serial, std::move(rmote));
          } else {
            if (check_up) {
              if (node.serial == dev.serial || node.serial.empty()) {
                node.serial = std::move(dev.serial);
                merge_adb_info(node, std::move(dev));
                adb_serials_.push_back(std::make_pair(dev.serial, node.identity));
                onDeviceInterfaceChanged(std::move(node));
                check_up = false;
              }
            }
          }
        }
      }

    if (check_up && round < 60) {
      auto identity = node.identity;
      adb_task_.push_request_conditional(Trigger { .node = std::move(node), .round = round + 1 }, [&identity](auto &r) {
        return r.node.identity == identity;
      });

      std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
  });
}

void UsbEnumerator::deleteAdbTask() {
  adb_task_.stop();
}


} // namespace device_enumerator
