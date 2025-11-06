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
#include <string>
#include <string_view>
#include <stdexcept>
#include <optional>
#include <chrono>
#include <vector>
#include <tuple>
#include <filesystem>

namespace adb_client {

class adb_error : public std::runtime_error {
public:
  adb_error(const std::string& arg): std::runtime_error(arg) {}
};

class adb_sync_error : public adb_error {
public:
  enum error_code {
    file_not_exists = 2,
    no_permission = 13,
  };

  adb_sync_error(const std::string& arg, int c): adb_error(arg), code((error_code)c) {}

  const error_code code;
};


enum class TransportType {
  Any,
  Usb,
  Local,
};

struct TransportOption {
  std::string_view server;
  std::string_view port;
  std::string_view serial;
  TransportType transportType{TransportType::Any};
  std::optional<int64_t> transportId;
  bool launchServerIfNeed{true};
};

struct DeviceInfo {
  std::string serial;
  std::string state;
  std::string product;
  std::string model;
  std::string device;
  int64_t transportId{0};
};


struct Stat {
  uint64_t dev{0};
  uint64_t ino{0};
  uint32_t mode{0};
  uint32_t nlink{0};
  uint32_t uid{0};
  uint32_t gid{0};
  uint64_t size{0};
  int64_t atime{0};
  int64_t mtime{0};
  int64_t ctime{0};

  bool isExe() const;
};

struct ListItem {
  std::string name;
  uint32_t mode{0};
  uint32_t size{0};
  uint32_t mtime{0};
};


void adb_kill(TransportOption option = {}) noexcept;

void adb_command(
    std::string_view command,
    TransportOption option = {},
    std::optional<std::chrono::milliseconds> timeout = std::nullopt);

std::string
adb_query(
    std::string_view service,
    TransportOption option = {});

std::string
adb_command_query(
    std::string_view command,
    TransportOption option = {});

void wait_device(
    std::string_view state = "device",
    TransportOption option = {},
    std::optional<std::chrono::milliseconds> timeout = std::nullopt);

std::vector<std::string>
adb_get_features(
    TransportOption option = {});

std::vector<char>
adb_command_connect(
    std::string_view command,
    TransportOption option = {});

std::tuple<uint8_t, std::vector<char>, std::vector<char>>
adb_execute_shell(
    std::string_view command,
    TransportOption option = {},
    std::optional<bool> use_shell_protocol = std::nullopt);

void adb_remount(
    TransportOption option = {},
    std::optional<bool> use_remount_shell = std::nullopt,
    std::string_view args = {});

void adb_root(bool root, TransportOption option = {});

std::vector<DeviceInfo>
adb_list_devices(
    TransportOption option = {},
    bool device_only = true, std::string_view target_serial = {});


Stat
sync_stat(
    std::string_view path,
    TransportOption option = {});

std::vector<ListItem>
sync_list(
    std::string_view path,
    TransportOption option = {});

void sync_pull(
    const std::vector<std::string>& srcs,
    const std::filesystem::path &dst,
    TransportOption option = {});

std::vector<char> sync_pull_buffer(
    std::string_view path,
    TransportOption option = {});

void sync_push(
    const std::vector<std::filesystem::path>& srcs,
    std::string_view dst,
    TransportOption option = {});

void sync_push_buffer(
    const char *buffer,
    size_t size,
    std::string_view dst,
    TransportOption option = {});

} // namespace adb_client
