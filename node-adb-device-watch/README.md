# NodeDevWatch

npm i node-adb-device-watch

```js
const { startDeviceWatch } = require('node-adb-device-watch');

const proc = await startDeviceWatch(['--types=usb,|net', '--watch'], data => {
      console.log(data);
    }, ['C:\\dev\\project_new\\dev_monitor\\build\\Release\\']);
    
proc.stop();
await proc.join();

```

## 许可证

MIT License

## 贡献
