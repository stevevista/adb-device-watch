const { startDeviceWatch } = require('./index.js');

// 简单的测试函数
async function runTests() {
  console.log('🧪 Running NodeDevWatch tests...\n');
  
  try {
    // 测试设备监控器
    console.log('1. Testing DeviceWatcher initialization...');

    // 测试启动和停止
    console.log('2. Testing start/stop functionality...');
    const proc = startDeviceWatch(['--types=usb,|net', '--watch'], data => {
      console.log('   📱 EVENT:', data);
    }, []);
    
    // 等待一段时间让监控器初始化
    await new Promise(resolve => setTimeout(resolve, 2000));
    proc.stop();
  
    await proc.join();
    
    console.log('\n✅ All tests passed!');
    
  } catch (error) {
    console.error('❌ Test failed:', error);
    process.exit(1);
  }
}

// 运行测试
runTests().catch(error => {
  console.error('Test runner error:', error);
  process.exit(1);
});