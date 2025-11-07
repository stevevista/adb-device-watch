# node-adb-device-watch

Node.js device monitoring tool for ADB and USB devices with pre-built executables.

## Installation

### For End Users (Recommended)

直接安装使用预编译的exe文件：

```bash
npm install node-adb-device-watch
```

安装后即可直接使用，无需额外编译步骤。

### For Developers

如果需要重新构建exe文件：

```bash
# 克隆项目
git clone https://github.com/stevevista/adb-device-watch.git
cd adb-device-watch

# 构建exe文件
npm run package:exe

# 测试
npm test

# 发布到npm
npm publish
```

## Features

- **Pre-built Executables**: Includes platform-specific binaries (Windows/Linux)
- **Automatic Path Detection**: Automatically finds and uses the bundled executable
- **Monitor ADB device changes**: Real-time ADB device monitoring
- **Monitor USB device changes**: USB device detection and status tracking
- **Real-time device status updates**: JSON format output for easy integration
- **Multiple filtering options**: Filter by VID, PID, type, driver
- **Support for network ADB devices**: Network-connected ADB device support

## Usage

### As a CLI tool

```bash
npx node-adb-device-watch --watch
```

### As a library (ES Modules)

```javascript
import { startDeviceWatch } from 'node-adb-device-watch';

// No need to specify extraSearchPaths - pre-built exe is automatically detected
const proc = await startDeviceWatch(['--types=usb,adb|net', '--watch'], (data) => {
  console.log('Device changed:', data);
});

// To stop monitoring
proc.stop();
await proc.join();
```

### As a library (CommonJS)

```javascript
const { startDeviceWatch } = require('node-adb-device-watch');

const proc = await startDeviceWatch(['--watch'], (data) => {
  console.log('Device changed:', data);
});

proc.stop();
await proc.join();
```

### Advanced Usage with Custom Paths

If you need to use a custom executable path:

```javascript
import { startDeviceWatch } from 'node-adb-device-watch';

// Custom executable paths will be searched in addition to the pre-built one
const proc = await startDeviceWatch(['--watch'], (data) => {
  console.log('Device changed:', data);
}, ['/custom/path/to/dev-watch']);
```

## Requirements

- Node.js >= 14.0.0
- **No additional dependencies required** - executables are pre-built and bundled

## License

MIT License - see [LICENSE](LICENSE) file for details.

## Contributing

Contributions are welcome! Please feel free to submit a Pull Request.
