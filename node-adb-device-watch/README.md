# node-adb-device-watch

Node.js device monitoring tool for ADB and USB devices.

## Installation

```bash
npm install node-adb-device-watch
```

## Usage

### As a CLI tool

```bash
npx node-adb-device-watch --watch
```

### As a library (ES Modules)

```javascript
import { startDeviceWatch } from 'node-adb-device-watch';

const proc = await startDeviceWatch(['--types=usb,adb|net', '--watch'], (data) => {
  console.log('Device changed:', data);
}, ['/path/to/dev-watch/binary']);

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

## Features

- Monitor ADB device changes
- Monitor USB device changes  
- Real-time device status updates
- JSON format output
- Multiple filtering options (VID, PID, type, driver)
- Support for network ADB devices

## Requirements

- Node.js >= 14.0.0
- dev-watch binary (see project documentation)

## License

MIT License - see [LICENSE](LICENSE) file for details.

## Contributing

Contributions are welcome! Please feel free to submit a Pull Request.
