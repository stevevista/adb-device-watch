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
#include "usb-watch-base.h"
#include <chrono>

namespace device_enumerator {

class UsbEnumeratorNetlink : public UsbEnumerator {
public:
  struct UsbInterfaceAttr;
  struct UsbSerialContext {
    int timeout{0};
    std::string devpath;
    uint16_t vid{0};
    uint16_t pid{0};
    int ifnum;
    bool is_diag{false};
    std::chrono::steady_clock::time_point time;
  };

  ~UsbEnumeratorNetlink();

  void deleteWatch() noexcept;

protected:
  int createNetlink();
  void enumerateDevices() override;
  bool poll(bool blocking);

private:
  void sysfs_usb_interface_enumerated(const UsbInterfaceAttr*);

  void load_driver();
  void unload_driver();

  int eventfd_{-1};
  int netlinkfd_{-1};
  UsbSerialContext expect_tty_;
  bool driver_manually_loaded_{false};
  int adb_booted_{0};
};

class UsbWatcherNetLink : public UsbEnumeratorNetlink {
public:
  void createWatch(std::function<void(bool)> &&cb) noexcept;
};

} // namespace device_enumerator
