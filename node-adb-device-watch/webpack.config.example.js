// Webpack配置示例，用于解决node-adb-device-watch在打包环境中的路径问题

const CopyWebpackPlugin = require('copy-webpack-plugin');
const path = require('path');

module.exports = {
  // ... 其他webpack配置
  
  // 解决方案1: 将模块标记为外部依赖
  externals: {
    'node-adb-device-watch': 'commonjs node-adb-device-watch'
  },
  
  // 或者解决方案2: 复制二进制文件到输出目录
  plugins: [
    new CopyWebpackPlugin({
      patterns: [
        {
          from: 'node_modules/node-adb-device-watch/bin',
          to: 'bin',
          noErrorOnMissing: true
        }
      ]
    })
  ],
  
  // 解决方案3: 使用webpack-node-externals
  // const nodeExternals = require('webpack-node-externals');
  // externals: [nodeExternals()],
};

// 在代码中使用示例：
/*
const { startDeviceWatch } = require('node-adb-device-watch');

const watcher = await startDeviceWatch(['--watch'], (device) => {
  console.log('Device changed:', device);
}, []);
*/
