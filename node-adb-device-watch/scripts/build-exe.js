const { existsSync, mkdirSync, copyFileSync } = require('fs');
const { join, dirname } = require('path');

async function prepareExe() {
  console.log('🚀 Preparing pre-built adb-device-watch executable...');
  
  const projectRoot = join(__dirname, '..', '..');
  const buildDir = join(projectRoot, 'build');
  const binDir = join(__dirname, '..', 'bin');
  
  try {
    // 确保bin目录存在
    if (!existsSync(binDir)) {
      mkdirSync(binDir, { recursive: true });
    }
    
    // 检查预编译的exe文件是否存在
    const exeName = process.platform === 'win32' ? 'adb-device-watch.exe' : 'adb-device-watch';
    const sourceExe = join(buildDir, 'Release', exeName);
    const targetDir = join(binDir, process.platform === 'win32' ? 'windows' : 'linux');
    
    if (!existsSync(targetDir)) {
      mkdirSync(targetDir, { recursive: true });
    }
    
    const targetExe = join(targetDir, exeName);
    
    if (existsSync(sourceExe)) {
      console.log(`📦 Using pre-built ${exeName}...`);
      copyFileSync(sourceExe, targetExe);
      console.log('✅ Pre-built executable prepared successfully!');
    } else {
      console.warn(`⚠️  Pre-built executable not found at: ${sourceExe}`);
      console.log('📝 Creating placeholder file for npm package...');
      
      // 创建占位符文件
      const placeholderContent = `# This is a placeholder for the pre-built adb-device-watch executable
# The actual executable should be built and placed here before npm publish
# For development, run: npm run build:exe
`;
      
      require('fs').writeFileSync(targetExe, placeholderContent);
      console.log('✅ Placeholder file created.');
    }
    
  } catch (error) {
    console.error('❌ Preparation failed:', error.message);
    process.exit(1);
  }
}

prepareExe();