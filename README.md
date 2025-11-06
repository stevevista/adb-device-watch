# DevMonitor - 设备监控工具

监视Adb和其他USB串口类设备的变化，相当于增强版的 `adb devices` 命令。

## 特性

- 增加了 USB hub 口属性，便于识别同一HUB口的设备
- 增加了驱动 (driver) 属性
- 以JSON格式输出设备信息
- 支持实时监控设备变化
- 支持多种过滤条件（VID、PID、类型、驱动等）
- 支持网络ADB设备监控

## 许可证

本项目采用 [MIT License](LICENSE) 开源许可证。

## 设备信息示例


```json
{
    "description": "Android Composite ADB Interface (0013)",
    "device": "frost",
    "driver": "WinUSB",
    "hub": "USB1-10",
    "id": "d034b076edae815d42780f4d2c5aacf1",
    "manufacturer": "Xiaomi",
    "model": "C3QP",
    "pid": 37009,
    "product": "frost",
    "serial": "a51302f7",
    "type": "usb,adb",
    "vid": 12783
}
```

# 参数
* `--pretty` - 缩进格式化输出，否则按行紧凑输出
* `--watch` - 监视设备变化，按任意键退出，否则只列出当前设备然后退出
* `--vids` - 过滤的 VID 列表，以逗号分隔，可以在vid 前添加 `!` 表示排除， 例如 `2341,!1234` 表示包含 VID 2341 但排除 VID 1234
* `--pids` - 过滤的 PID 列表，同 `--vids` 格式
* `--types` - 过滤的设备类型，以 | 和 , 分隔，| 表示或，, 表示且，例如 `usb,adb|net` 表示包含 USBADBADB 设备或网络设备
* `--drivers` - 过滤的 驱动 列表，以逗号分隔，例如 `qcserial,WinUSB` 表示包含 qcserial 和 WinUSB 驱动的设备
* `--ip_list` - 要监视的网络adb目标，以逗号分隔，例如 `192.168.1.100:5555,192.168.1.101:5555`， ’:5555‘ 可以省略

# cli
* adb-device-watch

# node js api
* node-adb-device-watch

# .NET api
* adb-device-watch-sharp
