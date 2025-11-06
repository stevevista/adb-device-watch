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

import chalk from 'chalk';

export async function startDeviceWatch(args, callback, extraSearchPaths = []) {
  const { spawn } = await import('child_process');
  const JSONStream = await import('JSONStream');
    
  const pathSeparator = process.platform === 'win32' ? ';' : ':';
    
  let customPath = '';
    
  if (extraSearchPaths.length > 0) {
    customPath = extraSearchPaths.join(pathSeparator) + pathSeparator;
  }

  const env = {
    ...process.env,
    PATH: customPath + process.env.PATH
  };
    
  const proc = spawn('adb-device-watch', ['devices', ...args], { env });

  console.log(chalk.green('✅ Device monitoring started'));
    
  proc.stdout.pipe(JSONStream.parse()).on('data', (data) => {
    callback(data);
  });
    
  proc.stderr.on('data', (data) => {
    console.error(chalk.red('❌ error:'), data.toString());
  });

  const join_process = new Promise((resolve, reject) => {
      proc.on('error', (error) => {
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
