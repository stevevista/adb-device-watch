using System;
using System.Collections.Generic;
using System.Text;
using System.Text.Json;
using System.Diagnostics;
using DevWatchSharp;

// dotnet C:\dev\project_new\dev_monitor\build\dev-watch-sharp\Release\dev-watch-sharp.dll

namespace Test
{
    class Program
    {
        static void Main(string[] args)
        {
            var p = DevWatch.StartWatching("--watch", (device) =>
            {
                if (device != null)
                {
                    Console.WriteLine($"设备状态变化:");
                    Console.WriteLine($"  ID: {device.id}");
                    Console.WriteLine($"  Type: {device.type}");
                    Console.WriteLine($"  Off: {device.off}");
                    Console.WriteLine($"  序列号: {device.serial}");
                    Console.WriteLine($"  描述: {device.description}");
                    Console.WriteLine($"  VID/PID: {device.vid:X4}/{device.pid:X4}");
                    Console.WriteLine($"  设备路径: {device.devpath}");
                    Console.WriteLine();
                }
            }, "C:\\dev\\project_new\\dev_monitor\\build\\Release");
            
            Console.WriteLine("按任意键退出...");
            Console.ReadKey();
            Console.WriteLine("Killing");
            p.Kill();
            Console.WriteLine("Killed");
            p.WaitForExit();
        }
    }
}
