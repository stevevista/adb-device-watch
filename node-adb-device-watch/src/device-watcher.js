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

const chalk = require('chalk');
const { dirname, join } = require('path');
const { existsSync } = require('fs');
const { spawn } = require('child_process');
const JSONStream = require('JSONStream');

/**
 * 获取当前模块的目录路径，兼容webpack等打包工具
 */
function getCurrentDirname() {
  // CommonJS环境下直接使用__dirname
  if (typeof __dirname !== 'undefined') {
    return __dirname;
  }
  
  try {
    // 回退方案：尝试使用 process.cwd()
    const possiblePaths = [
      // 开发环境
      join(process.cwd(), 'src'),
      // 打包后的环境
      join(process.cwd(), 'dist', 'src'),
      join(process.cwd(), 'lib', 'src'),
      // NPM包安装后的环境
      join(process.cwd(), 'node_modules', 'node-adb-device-watch', 'src'),
      process.cwd()
    ];
    
    // 查找包含当前文件的目录
    for (const dir of possiblePaths) {
      if (existsSync(dir)) {
        return dir;
      }
    }
  } catch (e) {
    // 最后回退到当前工作目录
  }
  
  return process.cwd();
}

/**
 * Webpack兼容性配置建议
 * 在webpack.config.js中添加：
 * externals: {
 *   'node-adb-device-watch': 'commonjs node-adb-device-watch'
 * }
 * 
 * 或者使用 copy-webpack-plugin 复制二进制文件：
 * new CopyWebpackPlugin({
 *   patterns: [
 *     { from: 'node_modules/node-adb-device-watch/bin', to: 'bin' }
 *   ]
 * })
 */

function startDeviceWatch(args, callback, extraSearchPaths = []) {
  const pathSeparator = process.platform === 'win32' ? ';' : ':';
  const executableName = process.platform === 'win32' ? 'adb-device-watch.exe' : 'adb-device-watch';
    
  // 定义多种可能的二进制文件路径
  const possibleBinPaths = [
    // 标准NPM包结构
    join(__dirname, '..', 'bin', process.platform === 'win32' ? 'windows' : 'linux'),
    join(__dirname, '..', 'bin'),
  ];
  
  // 查找实际存在的二进制文件
  const validBinPaths = [];
  for (const dir of possibleBinPaths) {
    const fullPath = join(dir, executableName);
    if (existsSync(fullPath)) {
      validBinPaths.push(dir);
      console.log(dir)
    }
  }
  
  const allSearchPaths = [...validBinPaths, ...extraSearchPaths];
  const customPath = allSearchPaths.length > 0 ? allSearchPaths.join(pathSeparator) + pathSeparator : '';

  const env = {
    ...process.env,
    PATH: customPath + process.env.PATH
  };
  
  const spawnOptions = {
    env,
    windowsHide: true
  };
  
  let proc;
  try {
    proc = spawn(executableName, [...args], spawnOptions);
  } catch (error) {
    console.error(chalk.red('❌ Failed to start adb-device-watch:'), error.message);
    console.error(chalk.yellow('💡 Suggestions:'));
    console.error(chalk.yellow('   1. Ensure adb-device-watch is installed: npm install -g adb-device-watch'));
    console.error(chalk.yellow('   2. Check if the binary exists in one of these paths:'));
    validBinPaths.forEach(path => console.error(chalk.yellow(`      - ${path}`)));
    console.error(chalk.yellow('   3. For webpack users, add this to your webpack config:'));
    console.error(chalk.yellow('      externals: { "node-adb-device-watch": "commonjs node-adb-device-watch" }'));
    throw error;
  }

  console.log(chalk.green('✅ Device monitoring started'));
    
  proc.stdout.pipe(JSONStream.parse()).on('data', (data) => {
    callback(data);
  });
    
  proc.stderr.on('data', (data) => {
    console.error(chalk.red('❌ error:'), data.toString());
  });

  const join_process = new Promise((resolve, reject) => {
      proc.on('error', (error) => {
        console.error(chalk.red('❌ error:'), error.message);
        reject(error);
      });

      proc.on('close', () => {
        console.log(chalk.green('✅ Device monitoring stopped'));
        resolve();
      });
  });

  return {
    stop() {
      proc.kill();
    },

    async join() {
      await join_process;
    }
  }
}

// CommonJS导出
module.exports = {
  startDeviceWatch
};
