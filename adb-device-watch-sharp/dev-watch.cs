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

using System;
using System.Collections.Generic;
using System.Text;
using System.Text.Json;
using System.Diagnostics;

// dotnet C:\dev\project_new\dev_monitor\build\dev-watch-sharp\Release\dev-watch-sharp.dll

namespace DevWatchSharp
{
  public class DeviceNode
    {
        public string id { get; set; }
        public bool off { get; set; }
        public string devpath { get; set; }
        public string hub { get; set; }
        public string serial { get; set; }
        public string manufacturer { get; set; }
        public string product { get; set; }
        public string model { get; set; }
        public string device { get; set; }
        public string driver { get; set; }
        public string ip { get; set; }
        public int port { get; set; }
        public string type { get; set; }
        public string description { get; set; }
        public int vid { get; set; }
        public int pid { get; set; }
        public int usbIf { get; set; }
        public int usbClass { get; set; }
        public int usbSubClass { get; set; }
        public int usbProto { get; set; }
    }

  public class DevWatch {
    static private Process CreateProcess(string args, Action<string> onOutput, string path = null) {
      var p = new Process();
      p.StartInfo = new ProcessStartInfo("adb-device-watch.exe", args);
      p.StartInfo.CreateNoWindow = true;
      p.StartInfo.UseShellExecute = false;
      p.StartInfo.RedirectStandardOutput = true;

      string originPath = Environment.GetEnvironmentVariable("PATH") ?? "";

      if (!string.IsNullOrEmpty(path)) {
        string pathvar = System.Environment.GetEnvironmentVariable("PATH");
        Environment.SetEnvironmentVariable("PATH", $"{path};{originPath}");
      }

      p.OutputDataReceived += (s, a) => {
        if (!string.IsNullOrEmpty(a.Data)) {
          onOutput?.Invoke(a.Data);
        }
      };

      p.Start();
      p.BeginOutputReadLine();

      if (!string.IsNullOrEmpty(path)) {
        Environment.SetEnvironmentVariable("PATH", originPath);
      }
    
      return p;
    }
    
    public static Process StartWatching(string args, Action<DeviceNode> onDeviceChanged, string path = null) {
      return CreateProcess(args, (jsonText) => {
        try {
          var deviceNode = System.Text.Json.JsonSerializer.Deserialize<DeviceNode>(jsonText);
          onDeviceChanged?.Invoke(deviceNode);
        } catch (JsonException ex) {
          Console.WriteLine($"JSON: {ex.Message}");
          Console.WriteLine($"data: {jsonText}");
        }
      }, path);
    }
  }
}
