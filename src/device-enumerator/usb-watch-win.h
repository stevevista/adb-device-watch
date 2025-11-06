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

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <setupapi.h>

#include <queue>
#include <tuple>

namespace device_enumerator {

class UsbEnumeratorWindows : public UsbEnumerator {
protected:
  UsbEnumeratorWindows() = default;

  bool OnDeviceWatchMessageHandleA(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam);
  bool OnDeviceWatchMessageHandleW(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam);

private:
  void enumerateDevices() override;

  void enumerateUsbInterfaces(const GUID *guid);
  void enumerateUsbInterfaces(
    const GUID *guid,
    const std::string &expect_devpath);

  void handleUsbInterfaceEnumated(
    const GUID *guid,
    HDEVINFO hDeviceInfo,
    const PSP_DEVINFO_DATA deviceInfoData,
    const std::string &interfaceDevpath
  );

  template<typename T>
  void OnDeviceChangeX(HWND hwnd, WPARAM wParam, LPARAM lParam);

  template<typename T>
  bool OnDeviceWatchMessageHandleX(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam);

  void notifyInterfaceArrival(const GUID *guid, const std::string &devpath);
  void notifyInterfaceRemove(const GUID *guid, const std::string &devpath);

  void onWatchCreate(HWND hWnd);
  void onWatchDestroy();

private:
  HWND hwnd_{nullptr};

  struct QueryRequest {
    const GUID *guid;
    std::string devpath;
    bool is_initial{false};
  };
};

class UsbWatcherWindows : public UsbEnumeratorWindows {
public:
  void createWatch(std::function<void(bool)> &&cb) noexcept;
  void deleteWatch() noexcept;

private:
  bool CreateHiddenWindowA();
  static LRESULT CALLBACK WndProcA(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam);

  HWND hwnd_{nullptr};
};

} // namespace device_enumerator

