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
#include "adb-client.h"
#include <asio/awaitable.hpp>


namespace adb_client {

asio::awaitable<void>
co_wait_device(
    std::string_view state = "device",
    TransportOption option = {},
    std::optional<std::chrono::milliseconds> timeout = std::nullopt);

asio::awaitable<void>
co_kill(TransportOption option = {}) noexcept;

asio::awaitable<std::string>
co_query(
    std::string_view service,
    TransportOption option = {});

asio::awaitable<std::vector<DeviceInfo>>
co_list_devices(TransportOption option, bool device_only = true, std::string_view target_serial = {});

asio::awaitable<void>
co_command(
    std::string_view command,
    TransportOption option = {},
    std::optional<std::chrono::milliseconds> timeout = std::nullopt);

asio::awaitable<std::string>
co_command_query(
    std::string_view command,
    TransportOption option = {});

asio::awaitable<std::vector<char>>
co_command_connect(
    std::string_view command,
    TransportOption option = {});

asio::awaitable<std::vector<std::string>>
co_get_features(
    TransportOption option = {});

asio::awaitable<std::tuple<uint8_t, std::vector<char>, std::vector<char>>>
co_execute_shell(
    std::string_view command,
    TransportOption option = {},
    std::optional<bool> use_shell_protocol = std::nullopt);

asio::awaitable<void>
co_remount(
    TransportOption option = {},
    std::optional<bool> use_remount_shell = std::nullopt,
    std::string_view args = {});

asio::awaitable<void>
co_root(bool root, TransportOption option = {});

asio::awaitable<std::vector<ListItem>>
co_sync_list(std::string_view path, TransportOption option = {});

asio::awaitable<void>
co_sync_pull(
    const std::vector<std::string>& srcs,
    const std::filesystem::path &dst,
    TransportOption option = {});

asio::awaitable<std::vector<char>>
co_sync_pull_buffer(
    std::string_view src,
    TransportOption option = {});

asio::awaitable<Stat>
co_sync_stat(
    std::string_view path,
    TransportOption option = {});

asio::awaitable<void>
co_sync_push(
    const std::vector<std::filesystem::path>& srcs,
    std::string_view dst,
    TransportOption option = {});

asio::awaitable<void>
co_sync_push_buffer(
    const char *buffer,
    size_t size,
    std::string_view dst,
    TransportOption option = {});

} // namespace adb_client
