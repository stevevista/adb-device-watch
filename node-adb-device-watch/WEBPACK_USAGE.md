# Webpack使用指南

## 问题描述

在使用webpack等打包工具时，`node-adb-device-watch`可能会因为路径解析问题而无法找到二进制文件。这是因为`import.meta.url`在打包环境中可能无法正确解析。

## 解决方案

### 方案1: 使用外部依赖（推荐）

在webpack配置中将模块标记为外部依赖：

```javascript
// webpack.config.js
module.exports = {
  externals: {
    'node-adb-device-watch': 'commonjs node-adb-device-watch'
  }
};
```

这样webpack不会打包这个模块，而是在运行时从node_modules中加载。

### 方案2: 复制二进制文件

使用copy-webpack-plugin将二进制文件复制到输出目录：

```javascript
const CopyWebpackPlugin = require('copy-webpack-plugin');

module.exports = {
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
  ]
};
```

## 使用示例

### 基本使用
```javascript
const { startDeviceWatch } = require('node-adb-device-watch');

// 启动设备监控
const watcher = await startDeviceWatch(['--watch'], (device) => {
  console.log('Device changed:', device);
});

// 停止监控
watcher.stop();
```

### 带额外搜索路径
```javascript
const { startDeviceWatch } = require('node-adb-device-watch');

// 添加额外的二进制文件搜索路径
const watcher = await startDeviceWatch(
  ['--watch'], 
  (device) => {
    console.log('Device changed:', device);
  },
  ['/usr/local/bin', '/opt/adb-tools'] // 额外搜索路径
);
```

### 错误处理
```javascript
try {
  const watcher = await startDeviceWatch(['--watch'], (device) => {
    console.log('Device changed:', device);
  });
} catch (error) {
  console.error('Failed to start device watcher:', error.message);
}
```

## 注意事项

1. **平台差异**: Windows系统使用`.exe`后缀，Linux/macOS不使用
2. **权限问题**: 确保二进制文件有执行权限
3. **环境变量**: 确保`adb`命令在PATH中可用
4. **防火墙**: Windows可能需要允许网络访问

## 故障排除

如果仍然遇到问题：

1. 检查二进制文件是否存在
2. 验证文件权限
3. 查看详细的错误信息输出
4. 考虑使用外部依赖方案