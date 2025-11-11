const { execSync } = require('child_process');
const { existsSync, mkdirSync, copyFileSync } = require('fs');
const { join, dirname } = require('path');

async function packageExe() {
  console.log('🚀 Packaging pre-built executables for npm...');
  
  const projectRoot = join(__dirname, '..', '..');
  const buildDir = join(projectRoot, 'build');
  const binDir = join(__dirname, '..', 'bin');
  
  try {
    // 确保bin目录存在
    if (!existsSync(binDir)) {
      mkdirSync(binDir, { recursive: true });
    }
    
    console.log('📦 Building executables for all platforms...');
    
    // 构建Windows版本
    console.log('🔨 Building Windows executable...');
    try {
      execSync('cmake -B build -DCMAKE_BUILD_TYPE=Release', { 
        cwd: projectRoot, 
        stdio: 'inherit' 
      });
      
      execSync('cmake --build build --config Release', { 
        cwd: projectRoot, 
        stdio: 'inherit' 
      });
      
      // 复制Windows exe
      const windowsTargetDir = join(binDir, 'windows');
      if (!existsSync(windowsTargetDir)) {
        mkdirSync(windowsTargetDir, { recursive: true });
      }
      
      const windowsExe = join(buildDir, 'Release', 'adb-device-watch.exe');
      if (existsSync(windowsExe)) {
        copyFileSync(windowsExe, join(windowsTargetDir, 'adb-device-watch.exe'));
        console.log('✅ Windows executable packaged successfully!');
      } else {
        console.warn('⚠️  Windows executable not found, skipping...');
      }
      
    } catch (error) {
      console.warn('⚠️  Windows build failed, skipping...');
    }
    
    return;
    // 注意：Linux构建需要在Linux环境下进行
    console.log('📝 Creating Linux placeholder (requires Linux build environment)...');
    const linuxTargetDir = join(binDir, 'linux');
    if (!existsSync(linuxTargetDir)) {
      mkdirSync(linuxTargetDir, { recursive: true });
    }
    
    // 创建Linux占位符文件
    const linuxPlaceholder = `#!/bin/bash
# This is a placeholder for the Linux version of adb-device-watch
# To build the Linux executable, run on a Linux system:
# cmake -B build -DCMAKE_BUILD_TYPE=Release
# cmake --build build --config Release
# cp build/adb-device-watch bin/linux/

echo "Linux version of adb-device-watch is not available in this package."
echo "Please build it manually on a Linux system."
exit 1
`;
    
    require('fs').writeFileSync(join(linuxTargetDir, 'adb-device-watch'), linuxPlaceholder);
    
    // 设置Linux文件权限
    if (process.platform !== 'win32') {
      execSync(`chmod +x ${join(linuxTargetDir, 'adb-device-watch')}`);
    }
    
    console.log('✅ Linux placeholder created.');
    console.log('🎉 All executables packaged successfully!');
    
  } catch (error) {
    console.error('❌ Packaging failed:', error.message);
    process.exit(1);
  }
}

packageExe();