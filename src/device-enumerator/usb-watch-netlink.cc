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

#include "usb-watch-netlink.h"
#include "process/process.h"
#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <poll.h>
#include <charconv>

#include <linux/netlink.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <sys/eventfd.h>

#define _DEBUG 0

#if defined(UNREFERENCED_PARAMETER)
#define UNUSED(var) UNREFERENCED_PARAMETER(var)
#else
#define UNUSED(var) do { (void)(var); } while(0)
#endif

#define usbi_warn(ctx, ...) UNUSED(ctx)
#define usbi_info(ctx, ...) UNUSED(ctx)
// 

#if _DEBUG
#define usbi_dbg(format, ...) fprintf(stderr, format "\n", ##__VA_ARGS__)
#define usbi_err(format, ...) fprintf(stderr, format "\n", ##__VA_ARGS__)
#else
#define usbi_dbg(...) do {} while (0)
#define usbi_err(...) do {} while (0)
#endif

#define MAX_PATH_LEN 512


namespace device_enumerator {

namespace {
/*

[QcomSerialPort.NTx86.6.1]

%QcomDevice70020%  = QportInstall00, USB\VID_31EF&PID_7002&MI_00
%QcomDevice70023%  = QportInstall00, USB\VID_31EF&PID_7002&MI_03
%QcomDevice70024%  = QportInstall00, USB\VID_31EF&PID_7002&MI_04
%QcomDevice71010%  = QportInstall00, USB\VID_31EF&PID_7101&MI_00
%QcomDevice71015%  = QportInstall00, USB\VID_31EF&PID_7101&MI_05
%QcomDevice71020%  = QportInstall00, USB\VID_31EF&PID_7102&MI_00
%QcomDevice71025%  = QportInstall00, USB\VID_31EF&PID_7102&MI_05
%QcomDevice71026%  = QportInstall00, USB\VID_31EF&PID_7102&MI_06
%QcomDevice80000%  = QportInstall00, USB\VID_31EF&PID_8000&MI_00
%QcomDevice80002%  = QportInstall00, USB\VID_31EF&PID_8000&MI_02
%QcomDevice80010%  = QportInstall00, USB\VID_31EF&PID_8001&MI_00
%QcomDevice80012%  = QportInstall00, USB\VID_31EF&PID_8001&MI_02
%QcomDevice80020%  = QportInstall00, USB\VID_31EF&PID_8002&MI_00
%QcomDevice80022%  = QportInstall00, USB\VID_31EF&PID_8002&MI_02
%QcomDevice90080%  = QportInstall00, USB\VID_31EF&PID_9008
%QcomDevice90000%  = QportInstall00, USB\VID_31EF&PID_9000&MI_00
%QcomDevice90001%  = QportInstall00, USB\VID_31EF&PID_9000&MI_01
%QcomDevice90010%  = QportInstall00, USB\VID_31EF&PID_9001&MI_00
%QcomDevice90011%  = QportInstall00, USB\VID_31EF&PID_9001&MI_01
%QcomDevice90020%  = QportInstall00, USB\VID_31EF&PID_9002&MI_00
%QcomDevice90021%  = QportInstall00, USB\VID_31EF&PID_9002&MI_01
%QcomDevice90030%  = QportInstall00, USB\VID_31EF&PID_9003&MI_00
%QcomDevice90031%  = QportInstall00, USB\VID_31EF&PID_9003&MI_01
%QcomDevice90032%  = QportInstall00, USB\VID_31EF&PID_9003&MI_02
%QcomDevice32001%  = QportInstall00, USB\VID_31EF&PID_3200&MI_01
%QcomDevice32002%  = QportInstall00, USB\VID_31EF&PID_3200&MI_02
%QcomDevice90040%  = QportInstall00, USB\VID_31EF&PID_9004&MI_00
%QcomDevice90050%  = QportInstall00, USB\VID_31EF&PID_9005&MI_00
%QcomDevice90060%  = QportInstall00, USB\VID_31EF&PID_9006&MI_00
%QcomDevice900A0%  = QportInstall00, USB\VID_31EF&PID_900A&MI_00
%QcomDevice900A1%  = QportInstall00, USB\VID_31EF&PID_900A&MI_01
%QcomDevice900B0%  = QportInstall00, USB\VID_31EF&PID_900B&MI_00
%QcomDevice900C0%  = QportInstall00, USB\VID_31EF&PID_900C&MI_00
%QcomDevice900C1%  = QportInstall00, USB\VID_31EF&PID_900C&MI_01
%QcomDevice900D0%  = QportInstall00, USB\VID_31EF&PID_900D&MI_00
%QcomDevice900D1%  = QportInstall00, USB\VID_31EF&PID_900D&MI_01
%QcomDevice900E0%  = QportInstall00, USB\VID_31EF&PID_900E
%QcomDevice900F0%  = QportInstall00, USB\VID_31EF&PID_900F&MI_00
%QcomDevice900F1%  = QportInstall00, USB\VID_31EF&PID_900F&MI_01
%QcomDevice90100%  = QportInstall00, USB\VID_31EF&PID_9010&MI_00
%QcomDevice90101%  = QportInstall00, USB\VID_31EF&PID_9010&MI_01
%QcomDevice90110%  = QportInstall00, USB\VID_31EF&PID_9011&MI_00
%QcomDevice90111%  = QportInstall00, USB\VID_31EF&PID_9011&MI_01
%QcomDevice90120%  = QportInstall00, USB\VID_31EF&PID_9012&MI_00
%QcomDevice90130%  = QportInstall00, USB\VID_31EF&PID_9013&MI_00
%QcomDevice90160%  = QportInstall00, USB\VID_31EF&PID_9016&MI_00
%QcomDevice90162%  = QportInstall00, USB\VID_31EF&PID_9016&MI_02
%QcomDevice90170%  = QportInstall00, USB\VID_31EF&PID_9017&MI_00
%QcomDevice90172%  = QportInstall00, USB\VID_31EF&PID_9017&MI_02
%QcomDevice90180%  = QportInstall00, USB\VID_31EF&PID_9018&MI_00
%QcomDevice90183%  = QportInstall00, USB\VID_31EF&PID_9018&MI_03
%QcomDevice90191%  = QportInstall00, USB\VID_31EF&PID_9019&MI_01
%QcomDevice90192%  = QportInstall00, USB\VID_31EF&PID_9019&MI_02
%QcomDevice901B1%  = QportInstall00, USB\VID_31EF&PID_901B&MI_01
%QcomDevice901C0%  = QportInstall00, USB\VID_31EF&PID_901C&MI_00
%QcomDevice90200%  = QportInstall00, USB\VID_31EF&PID_9020&MI_00
%QcomDevice90203%  = QportInstall00, USB\VID_31EF&PID_9020&MI_03
%QcomDevice90210%  = QportInstall00, USB\VID_31EF&PID_9021&MI_00
%QcomDevice90220%  = QportInstall00, USB\VID_31EF&PID_9022&MI_00
%QcomDevice901D0%  = QportInstall00, USB\VID_31EF&PID_901D&MI_00
%QcomDevice901F0%  = QportInstall00, USB\VID_31EF&PID_901F&MI_00
%QcomDevice90250%  = QportInstall00, USB\VID_31EF&PID_9025&MI_00
%QcomDevice90253%  = QportInstall00, USB\VID_31EF&PID_9025&MI_03
%QcomDevice90260%  = QportInstall00, USB\VID_31EF&PID_9026&MI_00
%QcomDevice90262%  = QportInstall00, USB\VID_31EF&PID_9026&MI_02
%QcomDevice90280%  = QportInstall00, USB\VID_31EF&PID_9028&MI_00
%QcomDevice90281%  = QportInstall00, USB\VID_31EF&PID_9028&MI_01
%QcomDevice90290%  = QportInstall00, USB\VID_31EF&PID_9029&MI_00
%QcomDevice90292%  = QportInstall00, USB\VID_31EF&PID_9029&MI_02
%QcomDevice902C2%  = QportInstall00, USB\VID_31EF&PID_902C&MI_02
%QcomDevice902D2%  = QportInstall00, USB\VID_31EF&PID_902D&MI_02
%QcomDevice902E2%  = QportInstall00, USB\VID_31EF&PID_902E&MI_02
%QcomDevice902E4%  = QportInstall00, USB\VID_31EF&PID_902E&MI_04
%QcomDevice902F2%  = QportInstall00, USB\VID_31EF&PID_902F&MI_02
%QcomDevice90303%  = QportInstall00, USB\VID_31EF&PID_9030&MI_03
%QcomDevice90310%  = QportInstall00, USB\VID_31EF&PID_9031&MI_00
%QcomDevice90311%  = QportInstall00, USB\VID_31EF&PID_9031&MI_01
%QcomDevice90314%  = QportInstall00, USB\VID_31EF&PID_9031&MI_04
%QcomDevice90320%  = QportInstall00, USB\VID_31EF&PID_9032&MI_00
%QcomDevice90321%  = QportInstall00, USB\VID_31EF&PID_9032&MI_01
%QcomDevice90323%  = QportInstall00, USB\VID_31EF&PID_9032&MI_03
%QcomDevice90330%  = QportInstall00, USB\VID_31EF&PID_9033&MI_00
%QcomDevice90331%  = QportInstall00, USB\VID_31EF&PID_9033&MI_01
%QcomDevice90340%  = QportInstall00, USB\VID_31EF&PID_9034&MI_00
%QcomDevice90341%  = QportInstall00, USB\VID_31EF&PID_9034&MI_01
%QcomDevice90350%  = QportInstall00, USB\VID_31EF&PID_9035&MI_00
%QcomDevice90351%  = QportInstall00, USB\VID_31EF&PID_9035&MI_01
%QcomDevice90360%  = QportInstall00, USB\VID_31EF&PID_9036&MI_00
%QcomDevice90361%  = QportInstall00, USB\VID_31EF&PID_9036&MI_01
%QcomDevice90370%  = QportInstall00, USB\VID_31EF&PID_9037&MI_00
%QcomDevice90371%  = QportInstall00, USB\VID_31EF&PID_9037&MI_01
%QcomDevice90380%  = QportInstall00, USB\VID_31EF&PID_9038&MI_00
%QcomDevice90381%  = QportInstall00, USB\VID_31EF&PID_9038&MI_01
%QcomDevice903A1%  = QportInstall00, USB\VID_31EF&PID_903A&MI_01
%QcomDevice903B0%  = QportInstall00, USB\VID_31EF&PID_903B&MI_00
%QcomDevice903B1%  = QportInstall00, USB\VID_31EF&PID_903B&MI_01
%QcomDevice903C0%  = QportInstall00, USB\VID_31EF&PID_903C&MI_00
%QcomDevice903C1%  = QportInstall00, USB\VID_31EF&PID_903C&MI_01
%QcomDevice903D0%  = QportInstall00, USB\VID_31EF&PID_903D&MI_00
%QcomDevice903E0%  = QportInstall00, USB\VID_31EF&PID_903E&MI_00
%QcomDevice903F1%  = QportInstall00, USB\VID_31EF&PID_903F&MI_01
%QcomDevice903F2%  = QportInstall00, USB\VID_31EF&PID_903F&MI_02
%QcomDevice90401%  = QportInstall00, USB\VID_31EF&PID_9040&MI_01
%QcomDevice90402%  = QportInstall00, USB\VID_31EF&PID_9040&MI_02
%QcomDevice90412%  = QportInstall00, USB\VID_31EF&PID_9041&MI_02
%QcomDevice90413%  = QportInstall00, USB\VID_31EF&PID_9041&MI_03
%QcomDevice90422%  = QportInstall00, USB\VID_31EF&PID_9042&MI_02
%QcomDevice90423%  = QportInstall00, USB\VID_31EF&PID_9042&MI_03
%QcomDevice90430%  = QportInstall00, USB\VID_31EF&PID_9043&MI_00
%QcomDevice90431%  = QportInstall00, USB\VID_31EF&PID_9043&MI_01
%QcomDevice90440%  = QportInstall00, USB\VID_31EF&PID_9044&MI_00
%QcomDevice90441%  = QportInstall00, USB\VID_31EF&PID_9044&MI_01
%QcomDevice90450%  = QportInstall00, USB\VID_31EF&PID_9045&MI_00
%QcomDevice90451%  = QportInstall00, USB\VID_31EF&PID_9045&MI_01
%QcomDevice90460%  = QportInstall00, USB\VID_31EF&PID_9046&MI_00
%QcomDevice90470%  = QportInstall00, USB\VID_31EF&PID_9047&MI_00
%QcomDevice90480%  = QportInstall00, USB\VID_31EF&PID_9048&MI_00
%QcomDevice90481%  = QportInstall00, USB\VID_31EF&PID_9048&MI_01
%QcomDevice90482%  = QportInstall00, USB\VID_31EF&PID_9048&MI_02
%QcomDevice90490%  = QportInstall00, USB\VID_31EF&PID_9049&MI_00
%QcomDevice904A0%  = QportInstall00, USB\VID_31EF&PID_904A&MI_00
%QcomDevice904B0%  = QportInstall00, USB\VID_31EF&PID_904B&MI_00
%QcomDevice904C0%  = QportInstall00, USB\VID_31EF&PID_904C&MI_00
%QcomDevice904C1%  = QportInstall00, USB\VID_31EF&PID_904C&MI_01
%QcomDevice904C2%  = QportInstall00, USB\VID_31EF&PID_904C&MI_02
%QcomDevice904F0%  = QportInstall00, USB\VID_31EF&PID_904F&MI_00
%QcomDevice904F1%  = QportInstall00, USB\VID_31EF&PID_904F&MI_01
%QcomDevice90500%  = QportInstall00, USB\VID_31EF&PID_9050&MI_00
%QcomDevice90501%  = QportInstall00, USB\VID_31EF&PID_9050&MI_01
%QcomDevice90510%  = QportInstall00, USB\VID_31EF&PID_9051&MI_00
%QcomDevice90511%  = QportInstall00, USB\VID_31EF&PID_9051&MI_01
%QcomDevice90512%  = QportInstall00, USB\VID_31EF&PID_9051&MI_02
%QcomDevice90520%  = QportInstall00, USB\VID_31EF&PID_9052&MI_00
%QcomDevice90521%  = QportInstall00, USB\VID_31EF&PID_9052&MI_01
%QcomDevice90522%  = QportInstall00, USB\VID_31EF&PID_9052&MI_02
%QcomDevice90530%  = QportInstall00, USB\VID_31EF&PID_9053&MI_00
%QcomDevice90531%  = QportInstall00, USB\VID_31EF&PID_9053&MI_01
%QcomDevice90534%  = QportInstall00, USB\VID_31EF&PID_9053&MI_04
%QcomDevice90540%  = QportInstall00, USB\VID_31EF&PID_9054&MI_00
%QcomDevice90541%  = QportInstall00, USB\VID_31EF&PID_9054&MI_01
%QcomDevice90543%  = QportInstall00, USB\VID_31EF&PID_9054&MI_03
%QcomDevice90550%  = QportInstall00, USB\VID_31EF&PID_9055&MI_00
%QcomDevice90551%  = QportInstall00, USB\VID_31EF&PID_9055&MI_01
%QcomDevice90560%  = QportInstall00, USB\VID_31EF&PID_9056&MI_00
%QcomDevice90592%  = QportInstall00, USB\VID_31EF&PID_9059&MI_02
%QcomDevice905A0%  = QportInstall00, USB\VID_31EF&PID_905A&MI_00
%QcomDevice905D0%  = QportInstall00, USB\VID_31EF&PID_905D&MI_00
%QcomDevice905E0%  = QportInstall00, USB\VID_31EF&PID_905E&MI_00
%QcomDevice905F0%  = QportInstall00, USB\VID_31EF&PID_905F&MI_00
%QcomDevice905F1%  = QportInstall00, USB\VID_31EF&PID_905F&MI_01
%QcomDevice90600%  = QportInstall00, USB\VID_31EF&PID_9060&MI_00
%QcomDevice90610%  = QportInstall00, USB\VID_31EF&PID_9061&MI_00
%QcomDevice90611%  = QportInstall00, USB\VID_31EF&PID_9061&MI_01
%QcomDevice90620%  = QportInstall00, USB\VID_31EF&PID_9062&MI_00
%QcomDevice90640%  = QportInstall00, USB\VID_31EF&PID_9064&MI_00

*/

// from windows driver inf
constexpr struct {
  uint16_t pid;
  int ifnum;
} diag_interfaces[] = {
  {0x3197, 1},
  {0x6000, 1},
  {0x6000, 2},
  {0x7000, 1},
  {0x7001, 1},
  {0x7001, 3},


  {0x9065, 0},
  {0x9065, 1},
  {0x9065, 2},
  {0x9066, 0},
  {0x9066, 1},
  {0x9066, 2},
  {0x9068, 0},
  {0x9069, 0},
  {0x9069, 1},
  {0x9069, 2},
  {0x9070, 0},
  {0x9070, 1},
  {0x9071, 0},
  {0x9071, 1},
  {0x9072, 0},
  {0x9072, 1},
  {0x9072, 2},
  {0x9073, 0},
  {0x9074, 0},
  {0x9074, 1},
  {0x9075, 0},
  {0x9075, 1},
  {0x9075, 2},
  {0x9076, 0},
  {0x9076, 1},
  {0x9077, 0},
  {0x9077, 1},
  {0x9078, 0},
  {0x9078, 1},
  {0x9079, 0},
  {0x9079, 1},
  {0x9079, 2},
  {0x9080, 0},
  {0x9080, 1},
  {0x9080, 2},
  {0x9081, 2},
  {0x9082, 2},
  {0x9083, 0},
  {0x9084, 0},
  {0x9085, 0},
  {0x9086, 2},
  {0x9086, 3},
  {0x9086, 4},
  {0x9087, 2},
  {0x9087, 3},
  {0x9087, 4},
  {0x9088, 1},
  {0x9088, 2},
  {0x9088, 3},
  {0x9089, 1},
  {0x9089, 2},
  {0x9089, 3},
  {0x908A, 0},
  {0x908A, 1},
  {0x908A, 2},
  {0x908B, 0},
  {0x908B, 1},
  {0x908E, 0},
  {0x908E, 1},
  {0x908E, 2},
  {0x908E, 3},
  {0x908F, 0},
  {0x908F, 1},
  {0x9090, 0},
  {0x9090, 1},
  {0x9091, 0},
  {0x9092, 0},
  {0x9093, 3},
  {0x9094, 0},
  {0x9094, 1},
  {0x9095, 0},
  {0x9095, 1},
  {0x9098, 0},
  {0x9098, 1},
  {0x9099, 0},
  {0x9099, 1},
  {0x909A, 0},
  {0x909A, 1},
  {0x909B, 0},
  {0x909B, 1},
  {0x909C, 0},
  {0x909C, 1},
  {0x909C, 2},
  {0x909D, 0},
  {0x909D, 1},
  {0x909D, 2},
  {0x909E, 0},
  {0x909E, 1},
  {0x909E, 2},
  {0x909E, 3},
  {0x909F, 0},
  {0x909F, 1},
  {0x909F, 2},
  {0x90A0, 0},
  {0x90A0, 1},
  {0x90A0, 2},
  {0x90A1, 0},
  {0x90A2, 0},
  {0x90A2, 1},
  {0x90A3, 0},
  {0x90A3, 1},
  {0x90A5, 0},
  {0x90A5, 1},
  {0x90A6, 0},
  {0x90A6, 1},
  {0x90A7, 0},
  {0x90A7, 3},
  {0x90A8, 0},
  {0x90A8, 1},
  {0x90A9, 0},
  {0x90A9, 3},
  {0x90AA, 0},
  {0x90AB, 0},
  {0x90AB, 1},
  {0x90AC, 0},
  {0x90AC, 1},
  {0x90AD, 0},
  {0x90AD, 3},
  {0x90AE, 0},
  {0x90AE, 1},
  {0x90AF, 0},
  {0x90AF, 1},
  {0x90B0, 0},
  {0x90B0, 2},
  {0x90B2, 0},
  {0x90B5, 3},
  {0x90B6, 3},
  {0x90B7, 0},
  {0x90B8, 0},
  {0x90B9, 0},
  {0x90BF, 2},
  {0x90C0, 2},
  {0x90C1, 1},
  {0x90C2, 0},
  {0x90C2, 1},
  {0x90C3, 0},
  {0x90C3, 1},
  {0x90C4, 0},
  {0x90C5, 0},
  {0x90C5, 2},
  {0x90C5, 3},
  {0x90C6, 0},
  {0x90C6, 2},
  {0x90C6, 3},
  {0x90C7, 0},
  {0x90C7, 2},
  {0x90C7, 3},
  {0x90C8, 0},
  {0x90C8, 1},
  {0x90C9, 0},
  {0x90C9, 1},
  {0x90CA, 0},
  {0x90CB, 0},
  {0x90CC, 0},
  {0x90CD, 0},
  {0x90D0, 1},
  {0x90D1, 1},
  {0x90D2, 0},
  {0x90D3, 0},
  {0x90D4, 0},
  {0x90D5, 0},
  {0x90D6, 0},
  {0x90D7, 0},
  {0x90D7, 1},
  {0x90D8, 0},
  {0x90D8, 1},
  {0x90D9, 0},
  {0x90D9, 1},
  {0x90DA, 0},
  {0x90DB, 0},
  {0x90DC, 0},
  {0x90DD, 0},
  {0x90DD, 1},
  {0x90DE, 0},
  {0x90DE, 1},
  {0x90DF, 0},
  {0x90E0, 0},
  {0x90E1, 0},
  {0x90E1, 1},
  {0x90E1, 2},
  {0x90E2, 3},
  {0x90E2, 4},
  {0x90E2, 5},
  {0x90E2, 6},
  {0x90E2, 10},
  {0x90E2, 12},
  {0x90E3, 0},
  {0x90E3, 1},
  {0x90E3, 2},
  {0x90E3, 3},
  {0x90E4, 0},
  {0x90E4, 1},
  {0x90E5, 0},
  {0x90E5, 1},
  {0x90E6, 2},
  {0x90E6, 3},
  {0x90E7, 2},
  {0x90E7, 3},
  {0x90E8, 2},
  {0x90E9, 2},
  {0x90EA, 4},
  {0x90EA, 5},
  {0x90EC, 0},
  {0x90ED, 3},
  {0x90EE, 3},
  {0x90EF, 4},
  {0x90F0, 4},
  {0x90F0, 6},
  {0x90F2, 0},
  {0x90F3, 0},
  {0x90F6, 0},
  {0x90F6, 1},
  {0x90F6, 2},
  {0x90F7, 0},
  {0x90F7, 1},
  {0x90F7, 2},
  {0x90F8, 2},
  {0x90F8, 3},
  {0x90F8, 4},
  {0x90F9, 2},
  {0x90F9, 3},
  {0x90F9, 4},
  {0x90FA, 0},
  {0x90FB, 0},
  {0x90FB, 1},
  {0x90FB, 2},
  {0x90FB, 3},
  {0x90FC, 0},
  {0x90FD, 0},
  {0x90FE, 0},
  {0x90FE, 1},
  {0x90FE, 2},
  {0x90FF, 0},
  {0x90FF, 1},
  {0x9102, 0},
  {0x9103, 0},
  {0x9104, 0},
  {0x9105, 0},
  {0x9106, 0},
  {0x9107, 0},
  {0x9108, 0},
  {0x9109, 0},
  {0x910A, 0},
  {0x910B, 0},
  {0x910C, 0},
  {0x910D, 0},
  {0x910E, 0},
  {0x910E, 2},
  {0x910F, 0},
  {0x910F, 2},
  {0x9110, 0},
  {0x9110, 1},
  {0x9111, 0},
  {0x9111, 1},
  {0x9112, 0},
  {0x9112, 1},
  {0x9112, 3},
  {0x9113, 0},
  {0x9114, 0},
  {0x9115, 0},
  {0x9100, 0},
  {0x9100, 1},
  {0x9101, 0},
  {0x9101, 1},
  {0x9101, 2},
  {0x9101, 4},
  {0x9101, 5},
  {0x9402, 0},
  {0x9402, 1},
  {0x9404, 0},
  {0x9404, 1},
  {0x3199, 1},
  {0x3199, 2},
  {0x3199, 3},
  {0x319A, 1},
  {0x319A, 2},
  {0x319A, 3},
  {0x319B, 0},
  {0x319B, 2},
  {0x319B, 3},
  {0xF00D, 0},
  {0x920C, 0},
  {0x920D, 1},
  {0x920D, 3},
  {0x9311, 0},
  {0x9505, 0},
  {0x9680, 0},
  {0x9008, 0},
  {0,      0},
};

constexpr struct {
  uint16_t vid;
  uint16_t pid;
  int type; // 0 bootrom 1 tl other: unknown
} edl_interfaces[] = {
  {JLQ_VID,  0x5100, 0},
  {JLQ_VID,  0x5520, 0},
  {JLQ_VID,  0x5101, 1},
  {JLQ_VID,  0x5521, 1},
  {JLQ_VID,  0x3001, -1},
  {0,  0, 0},
};

bool is_edl_port(const uint16_t vid, const uint16_t pid, DeviceState &state) {
  for (int i = 0; edl_interfaces[i].vid; ++i) {
    if (edl_interfaces[i].vid == vid && edl_interfaces[i].pid == pid) {
      if (edl_interfaces[i].type == 0) {
        state = DeviceState::EDL;
      } else if (edl_interfaces[i].type == 0) {
        state = DeviceState::TL;
      } else {
        state = DeviceState::USB;
      }
      return true;
    }
  }
  return false;
}

bool is_diag_port(const uint16_t vid, const uint16_t pid, int ifnum) {
  DeviceState st;
  if (is_edl_port(vid, pid, st)) {
    return false;
  }

  if (vid == JLQ_VID || vid == QUALCOMM_VID) {
    for (int i = 0; diag_interfaces[i].pid; ++i) {
      if (diag_interfaces[i].pid == pid && diag_interfaces[i].ifnum == ifnum) {
        return true;
      }
    }
  }

  return false;
}

} // namespace

struct UsbEnumeratorNetlink::UsbInterfaceAttr {
  uint8_t numinterfaces;
  uint8_t busnum;
  uint8_t devaddr;
  uint16_t vendor{0};
  uint16_t product{0};
  std::string identity;
  std::string tty;
  std::string serial;
  std::string productDesc;
  bool is_adb{false};
  bool is_fastboot{false};
  bool is_hdc{false};
  int ifnum{-1};
};

namespace {

#define ADB_CLASS 0xff
#define ADB_SUBCLASS 0x42
#define ADB_PROTOCOL 0x1
#define FASTBOOT_PROTOCOL 0x3
#define HDC_SUBCLASS 0x50
#define HDC_PROTOCOL 0x1

using UsbInterfaceAttrs = UsbEnumeratorNetlink::UsbInterfaceAttr;
using UsbSerialContext = UsbEnumeratorNetlink::UsbSerialContext;

int is_adb_interface(int usb_class, int usb_subclass, int usb_protocol) {
  return (usb_class == ADB_CLASS && usb_subclass == ADB_SUBCLASS && usb_protocol == ADB_PROTOCOL);
}

int is_fastboot_interface(int usb_class, int usb_subclass, int usb_protocol) {
  return (usb_class == ADB_CLASS && usb_subclass == ADB_SUBCLASS && usb_protocol == FASTBOOT_PROTOCOL);
}

int is_hdc_interface(int usb_class, int usb_subclass, int usb_protocol) {
  return (usb_class == 0xff && usb_subclass == HDC_SUBCLASS && usb_protocol == HDC_PROTOCOL);
}

#define SYSFS_DEVICE_PATH "/sys/bus/usb/devices"

#define NL_GROUP_KERNEL 1

#ifndef SOCK_CLOEXEC
#define SOCK_CLOEXEC 0
#endif

#ifndef SOCK_NONBLOCK
#define SOCK_NONBLOCK 0
#endif

int set_fd_cloexec_nb(int fd, int socktype)
{
  int flags;

#if defined(FD_CLOEXEC)
  /* Make sure the netlink socket file descriptor is marked as CLOEXEC */
  if (!(socktype & SOCK_CLOEXEC)) {
    flags = fcntl(fd, F_GETFD);
    if (flags == -1) {
      usbi_err("failed to get netlink fd flags, errno=%d", errno);
      return -1;
    }

    if (fcntl(fd, F_SETFD, flags | FD_CLOEXEC) == -1) {
      usbi_err("failed to set netlink fd flags, errno=%d", errno);
      return -1;
    }
  }
#endif

  /* Make sure the netlink socket is non-blocking */
  if (!(socktype & SOCK_NONBLOCK)) {
    flags = fcntl(fd, F_GETFL);
    if (flags == -1) {
      usbi_err("failed to get netlink fd status flags, errno=%d", errno);
      return -1;
    }

    if (fcntl(fd, F_SETFL, flags | O_NONBLOCK) == -1) {
      usbi_err("failed to set netlink fd status flags, errno=%d", errno);
      return -1;
    }
  }

  return 0;
}

#if _DEBUG

void netlink_message_dump(const char *buffer, size_t len)
{
  usbi_dbg("-----------------------------------------");
  const char *end = buffer + len;
  while (buffer < end && *buffer) {
    usbi_dbg(">> %s", buffer);
    buffer += strlen(buffer) + 1;
  }
}

#else

inline void netlink_message_dump(const char *buffer, size_t len) {}

#endif

const char *netlink_message_parse(const char *buffer, size_t len, const char *key)
{
  const char *end = buffer + len;
  size_t keylen = strlen(key);

  while (buffer < end && *buffer) {
    if (strncmp(buffer, key, keylen) == 0 && buffer[keylen] == '=')
      return buffer + keylen + 1;
    buffer += strlen(buffer) + 1;
  }

  return nullptr;
}

int _sysfs_read_attr(const char *sysfs_dir, const char *attr, int *value_p, bool hex)
{
  char buf[20], *endptr;
  long value;
  ssize_t r;

  char attr_path[MAX_PATH_LEN];
  snprintf(attr_path, sizeof(attr_path), "%s/%s", sysfs_dir, attr);
  int fd = open(attr_path, O_RDONLY | O_CLOEXEC);
  if (fd < 0)
    return fd;

  r = read(fd, buf, sizeof(buf));
  close(fd);
  if (r < 0) {
    return r;
  }

  if (r == 0) {
    /* Certain attributes (e.g. bConfigurationValue) are not
     * populated if the device is not configured. */
    return -1;
  }

  buf[r - 1] = '\0';
  errno = 0;

  if (hex) {
    value = strtol(buf, &endptr, 16);
    if (value < 0 || errno) {
      return -2;
    }
  } else {
    value = strtol(buf, &endptr, 10);
    if (value < 0|| errno) {
      return -2;
    } else if (*endptr != '\0') {
      /* Consider the value to be valid if the remainder is a '.'
      * character followed by numbers.  This occurs, for example,
      * when reading the "speed" attribute for a low-speed device
      * (e.g. "1.5") */
      if (*endptr == '.' && isdigit(*(endptr + 1))) {
        endptr++;
        while (isdigit(*endptr))
          endptr++;
      }
      if (*endptr != '\0') {
        return -2;
      }
    }
  }

  *value_p = (int)value;
  return 0;
}

template <typename T>
int sysfs_read_attr(const char *sysfs_dir, const char *attr, T &value, bool hex) {
  int sysfs_val;
  int r = _sysfs_read_attr(sysfs_dir, attr, &sysfs_val, hex);
  if (r == 0) {
    value = static_cast<T>(sysfs_val);
  }

  return r;
}

template <>
int sysfs_read_attr<std::string>(const char *sysfs_dir, const char *attr, std::string &value, bool) {
  char attr_path[MAX_PATH_LEN];
  snprintf(attr_path, sizeof(attr_path), "%s/%s", sysfs_dir, attr);
  int fd = open(attr_path, O_RDONLY | O_CLOEXEC);
  if (fd > 0) {
    char buf[MAX_PATH_LEN];
    int r = read(fd, buf, sizeof(buf) - 1);
    close(fd);
    if (r > 0) {
      buf[r--] = 0;
      if (r > 0 && buf[r] == '\n') {
        buf[r] = 0;
      }
      value = buf;
      return 0;
    }
  }

  return -1;
}

int sysfs_get_usb_attributes(const char *device_dir, UsbInterfaceAttrs &attr) {
  int r = sysfs_read_attr(device_dir, "bNumInterfaces", attr.numinterfaces, false);
  if (r < 0) 
    return r;

  r = sysfs_read_attr(device_dir, "busnum", attr.busnum, false);
  if (r < 0) 
    return r;

  r = sysfs_read_attr(device_dir, "devnum", attr.devaddr, false);
  if (r < 0)
    return r;

  if (attr.vendor == 0) {
    r = sysfs_read_attr(device_dir, "idVendor", attr.vendor, true);
    if (r < 0) 
      return r;
  }

  if (attr.product == 0) {
    r = sysfs_read_attr(device_dir, "idProduct", attr.product, true);
    if (r < 0) 
      return r;
  }

  // read serial
  sysfs_read_attr(device_dir, "serial", attr.serial, false);

  sysfs_read_attr(device_dir, "product", attr.productDesc, false);

  char identity[256] = "USB";
  const char *p = strrchr(device_dir, '/');
  strcat(identity, p + 1);
  char *i = &identity[3];
  while (*i) {
    if (*i == '.') *i = '-';
    i++;
  }
  attr.identity = identity;

  return 0;
}

int sysfs_get_usb_interface_adb(
    const char *device_dir,
    UsbInterfaceAttrs &attr,
    const std::function<void(const UsbInterfaceAttrs*)> &onInterfaceEnumerated) {

  if (sysfs_get_usb_attributes(device_dir, attr) != 0) {
    return -1;
  }

  onInterfaceEnumerated(&attr);
  return 0;
}

int sysfs_get_usb_interface_adb(
    const char *interface_dir,
    const char *device_dir,
    UsbInterfaceAttrs &attr,
    const std::function<void(const UsbInterfaceAttrs*)> &onInterfaceEnumerated) {
  int usb_class, usb_subclass, usb_protocol;
  int r = sysfs_read_attr(interface_dir, "bInterfaceClass", usb_class, true);
  if (r < 0) 
    return r;

  r = sysfs_read_attr(interface_dir, "bInterfaceSubClass", usb_subclass, true);
  if (r < 0) 
    return r;

  r = sysfs_read_attr(interface_dir, "bInterfaceProtocol", usb_protocol, true);
  if (r < 0) 
    return r;

  bool is_adb = is_adb_interface(usb_class, usb_subclass, usb_protocol);
  bool is_fastboot = is_fastboot_interface(usb_class, usb_subclass, usb_protocol);
  bool is_hdc = is_hdc_interface(usb_class, usb_subclass, usb_protocol);

  if (!is_adb && !is_fastboot && !is_hdc) {
    return -1;
  }

  attr.is_adb = is_adb;
  attr.is_fastboot = is_fastboot;
  attr.is_hdc = is_hdc;

  return sysfs_get_usb_interface_adb(
    device_dir,
    attr,
    onInterfaceEnumerated);
}

int sysfs_get_usb_interface_tty_devname(
    const char *interface_dir,
    UsbInterfaceAttrs &attr,
    const std::function<void(const UsbInterfaceAttrs*)> &onInterfaceEnumerated) {
  DIR *dir = opendir(interface_dir); 
  if (!dir) {
    return -1;
  }

  struct dirent *entry;
  while ((entry = readdir(dir))) {
    if (strncmp(entry->d_name, "tty", 3) == 0) {
      closedir(dir);

      attr.tty = entry->d_name;

      onInterfaceEnumerated(&attr);
      return 0;
    }
  }

  closedir(dir);
  return -1;
}

int sysfs_get_usb_interface_tty(
    const char *device_dir,
    UsbInterfaceAttrs &attr,
    int ifnum,
    const std::function<void(const UsbInterfaceAttrs*)> &onInterfaceEnumerated) {
  if (sysfs_get_usb_attributes(device_dir, attr) != 0) {
    return -1;
  }

  onInterfaceEnumerated(&attr);
  return 0;
}

void set_expect_tty_usbserial(
    UsbSerialContext &ttyCtx,
    uint16_t vid,
    uint16_t pid,
    const std::string &devpath,
    const std::vector<int> ifnums,
    int timeout) {
  if (settings_.enableModprobe) {
    DeviceState state;
    bool is_edl = false;
    bool is_diag = false;
    int ifnum = 0;

    if (ifnums.size() == 1 && is_edl_port(vid, pid, state)) {
      is_edl = true;
      ifnum = ifnums[0];
    } else {
      for (auto num : ifnums) {
        if (is_diag_port(vid, pid, num)) {
          is_diag = true;
          ifnum = num;
          break;
        }
      }
    }

    if (is_edl || is_diag) {
      // a tty subssystem device should emerge
      // if not, we dynamiclly do mobprobe usbserial
      ttyCtx.timeout = timeout;
      ttyCtx.devpath = devpath;
      ttyCtx.vid = vid;
      ttyCtx.pid = pid;
      ttyCtx.is_diag = is_diag;
      ttyCtx.ifnum = ifnum;
      ttyCtx.time = std::chrono::steady_clock::now();
    }
  }
}

int sysfs_get_usb_device(
    const char *device_dir,
    UsbSerialContext &ttyCtx,
    const std::function<void(const UsbInterfaceAttrs*)> &onInterfaceEnumerated) {
  DIR *interfaces = opendir(device_dir);
  if (!interfaces) {
    return -1;
  }

  bool ttyFound = false;
  std::vector<int> unknownIfs;
  UsbInterfaceAttrs attr;
  if (sysfs_get_usb_attributes(device_dir, attr) != 0) {
    return -1;
  }

  struct dirent *entry;
  while ((entry = readdir(interfaces))) {
    if (!strchr(entry->d_name, ':'))
      continue;

    char interface_dir[MAX_PATH_LEN];
    snprintf(interface_dir, sizeof(interface_dir), "%s/%s", device_dir, entry->d_name);

    // parse interface number ...1:1.[0]
    int ifnum = -1;
    auto ifnumstr = strrchr(entry->d_name, '.');
    if (ifnumstr) {
      ifnumstr++;
      ifnum = strtol(ifnumstr, nullptr, 10);
    }

    attr.ifnum = ifnum;
  
    if (sysfs_get_usb_interface_tty_devname(interface_dir, attr, onInterfaceEnumerated) == 0) {
      ttyFound = true;
      continue;
    }

    if (sysfs_get_usb_interface_adb(interface_dir, device_dir, attr, onInterfaceEnumerated) == 0) {
      continue;
    }

    // parse interface number ...1:1.[0]
    if (ifnum >= 0) {
      unknownIfs.push_back(ifnum);
    }
  }

  if (!ttyFound && unknownIfs.size()) {
    set_expect_tty_usbserial(
      ttyCtx,
      attr.vendor,
      attr.product,
      "",
      {unknownIfs},
      1); // expire immerately
  }

  return 0;
}

int sysfs_get_device_list(UsbSerialContext &ttyCtx, const std::function<void(const UsbInterfaceAttrs*)> &onInterfaceEnumerated) {
  constexpr const char *sysfs_device_path = SYSFS_DEVICE_PATH;

  DIR *devices = opendir(sysfs_device_path);
  if (!devices) {
    return -1;
  }

  struct dirent *entry;
  while ((entry = readdir(devices))) {
    if (!isdigit(entry->d_name[0])
        || strchr(entry->d_name, ':'))
      continue;

    char device_dir[MAX_PATH_LEN];
    snprintf(device_dir, sizeof(device_dir), "%s/%s", sysfs_device_path, entry->d_name);

    sysfs_get_usb_device(device_dir, ttyCtx, onInterfaceEnumerated);
  }

  closedir(devices);
  return 0;
}

std::tuple<uint16_t, uint16_t, uint16_t> 
unpack_value(const char *str, int type) {
  uint16_t v[3] = { 0, 0, 0 };
  const char *p0 = str;

  for (int i = 0; i < 3; i++) {
    const char *p1 = (i == 2) ? (p0 + strlen(p0)) : strchr(p0, '/');
    if (!p1) {
      usbi_err("bad value format, %s", str);
      break;
    }
    std::from_chars(p0, p1, v[i], type);
    p0 = p1 + 1;
  }

  return { v[0], v[1], v[2] };
}

int linux_netlink_parse_usb_interface_add(
    const char *buffer,
    size_t len,
    UsbSerialContext &ttyCtx,
    const std::function<void(const UsbInterfaceAttrs*)> &onInterfaceEnumerated)
{
  // PRODUCT=31ef/3001/0
  // INTERFACE=255/255/255
  // DEVPATH=/devices/pci0000:00/0000:00:14.0/usb1/1-9/1-9.1/1-9.1:1.0
  const char *product = netlink_message_parse(buffer, len, "PRODUCT");
  const char *interface = netlink_message_parse(buffer, len, "INTERFACE");
  const char *devpath = netlink_message_parse(buffer, len, "DEVPATH");
  if (!product || !interface || !devpath) {
    usbi_err("usb_interface without PRODUCT | INTERFACE | DEVPATH value");
    return -1;
  }

  // parse interface number ...1:1.[0]
  int ifnum = -1;
  auto ifnumstr = strrchr(devpath, '.');
  if (ifnumstr) {
    ifnumstr++;
    ifnum = strtol(ifnumstr, nullptr, 10);
  }

  auto [vid, pid, _] = unpack_value(product, 16);
  auto [cls, subclass, proto] = unpack_value(interface, 10);

  bool is_adb = is_adb_interface(cls, subclass, proto);
  bool is_fastboot = is_fastboot_interface(cls, subclass, proto);
  bool is_hdc = is_hdc_interface(cls, subclass, proto);

  if (is_adb || is_fastboot || is_hdc) {
    // construct device_path from devpath
    // strip off last interface entry
    char device_dir[MAX_PATH_LEN] = "/sys";
    strcat(device_dir, devpath);
    // strip off "/1-9.1:1.0"
    char *slash = strrchr(device_dir, '/');
    *slash = 0;

    UsbInterfaceAttrs attr;
    attr.vendor = vid;
    attr.product = pid;
    attr.is_adb = is_adb;
    attr.is_fastboot = is_fastboot;
    attr.is_hdc = is_hdc;

    return sysfs_get_usb_interface_adb(
      device_dir,
      attr,
      onInterfaceEnumerated);
  }

  set_expect_tty_usbserial(
      ttyCtx,
      vid,
      pid,
      devpath,
      {ifnum},
      1000);

  return -1;
}

int linux_netlink_parse_usb_add(
    const char *buffer,
    size_t len,
    UsbSerialContext &ttyCtx,
    const std::function<void(const UsbInterfaceAttrs*)> &onInterfaceEnumerated)
{
  // DEVTYPE=usb_interface
  const char *devtype = netlink_message_parse(buffer, len, "DEVTYPE");
  if (devtype && strcmp(devtype, "usb_interface") == 0) {
    return linux_netlink_parse_usb_interface_add(buffer, len, ttyCtx, onInterfaceEnumerated);
  }

  return -1;
}

int linux_netlink_parse_tty_add(
    const char *buffer,
    size_t len,
    UsbSerialContext &ttyCtx,
    const std::function<void(const UsbInterfaceAttrs*)> &onInterfaceEnumerated)
{
  // DEVPATH=/devices/pci0000:00/0000:00:14.0/usb1/1-9/1-9.1/1-9.1:1.0/ttyUSB0/tty/ttyUSB0
  // DEVNAME=ttyUSB0
  const char *devname = netlink_message_parse(buffer, len, "DEVNAME");
  const char *devpath = netlink_message_parse(buffer, len, "DEVPATH");
  if (!devname || !devpath) {
    usbi_err("tty without DEVNAME | DEVPATH value");
    return -1;
  }

  // device has appeared
  // cancel pending tty expection
  if (ttyCtx.timeout > 0 && ttyCtx.devpath.size()) {
    if (strstr(devpath, ttyCtx.devpath.c_str()) == devpath) {
      ttyCtx.timeout = 0;
    }
  }

  // construct device_path from devpath
  // strip off last interface entry
  char device_dir[MAX_PATH_LEN] = "/sys";
  strcat(device_dir, devpath);
  // strip off "/1-9.1:1.0/ttyUSB0/tty/ttyUSB0"
  char *slash = strrchr(device_dir, ':');
  if (!slash) {
    return -1;
  }

  // parse ifnum
  int ifnum = -1;
  auto ifnumstr = strchr(slash + 1, '.');
  if (ifnumstr) {
    ifnum = strtol(ifnumstr + 1, nullptr, 10);
  }

  *slash = 0;
  
  slash = strrchr(device_dir, '/');
  *slash = 0;

  UsbInterfaceAttrs attr;
  attr.tty = devname;
  attr.ifnum = ifnum;

  return sysfs_get_usb_interface_tty(
    device_dir,
    attr,
    ifnum,
    onInterfaceEnumerated);
}

int linux_netlink_parse_action_add(
    const char *buffer,
    size_t len,
    UsbSerialContext &ttyCtx,
    const std::function<void(const UsbInterfaceAttrs*)> &onInterfaceEnumerated)
{
  const char *subsystem = netlink_message_parse(buffer, len, "SUBSYSTEM");
  if (subsystem && strcmp(subsystem, "usb") == 0) {
    return linux_netlink_parse_usb_add(buffer, len, ttyCtx, onInterfaceEnumerated);
  }

  if (subsystem && strcmp(subsystem, "tty") == 0) {
    return linux_netlink_parse_tty_add(buffer, len, ttyCtx, onInterfaceEnumerated);
  }

  return -1;
}

int linux_netlink_parse_action_remove(
    const char *buffer,
    size_t len,
    UsbSerialContext &ttyCtx,
    const std::function<void(uint8_t busnum, uint8_t devaddr)> &onUsbOff)
{

  // DEVPATH=/devices/pci0000:00/0000:00:14.0/usb1/1-9/1-9.1
  // SUBSYSTEM=usb
  // DEVTYPE=usb_device
  // BUSNUM=001
  // DEVNUM=016

  const char *subsystem = netlink_message_parse(buffer, len, "SUBSYSTEM");
  if (!subsystem || strcmp(subsystem, "usb") != 0) {
    return -1;
  }

  // cancel pending tty
  const char *devtype = netlink_message_parse(buffer, len, "DEVTYPE");
  if (devtype && strcmp(devtype, "usb_interface") == 0) {
    const char *devpath = netlink_message_parse(buffer, len, "DEVPATH");
    if (devpath && ttyCtx.timeout > 0 && ttyCtx.devpath == devpath) {
      ttyCtx.timeout = 0;
    }
  }

  uint8_t busnum, devaddr;
  errno = 0;

  const char *tmp = netlink_message_parse(buffer, len, "BUSNUM");
  if (tmp) {
      busnum = (uint8_t)(strtoul(tmp, NULL, 10) & 0xff);
      if (errno) {
        errno = 0;
        return -1;
      }

      tmp = netlink_message_parse(buffer, len, "DEVNUM");
      if (NULL == tmp)
        return -1;

      devaddr = (uint8_t)(strtoul(tmp, NULL, 10) & 0xff);
      if (errno) {
        errno = 0;
        return -1;
      }

    onUsbOff(busnum, devaddr);
    return 0;
  }

  return -1;
}

// parse parts of netlink message common to both libudev and the kernel
int linux_netlink_parse(
    const char *buffer,
    size_t len,
    UsbSerialContext &ttyCtx,
    const std::function<void(const UsbInterfaceAttrs*)> &onInterfaceEnumerated,
    const std::function<void(uint8_t busnum, uint8_t devaddr)> &onUsbOff)
{
  netlink_message_dump(buffer, len);

  const char *action = netlink_message_parse(buffer, len, "ACTION");
  if (action && strcmp(action, "add") == 0) {
    return linux_netlink_parse_action_add(buffer, len, ttyCtx, onInterfaceEnumerated);
  } else if (action && strcmp(action, "remove") == 0) {
    return linux_netlink_parse_action_remove(buffer, len, ttyCtx, onUsbOff);
  }

  return -1;
}

int linux_netlink_read_message(
    int fd,
    UsbSerialContext &ttyCtx,
    const std::function<void(const UsbInterfaceAttrs*)> &onInterfaceEnumerated,
    const std::function<void(uint8_t busnum, uint8_t devaddr)> &onUsbOff)
{
  char cred_buffer[CMSG_SPACE(sizeof(struct ucred))];
  char msg_buffer[2048];

  ssize_t len;
  struct cmsghdr *cmsg;
  struct ucred *cred;
  struct sockaddr_nl sa_nl;
  struct iovec iov = { .iov_base = msg_buffer, .iov_len = sizeof(msg_buffer) };
  struct msghdr msg = {
    .msg_name = &sa_nl, .msg_namelen = sizeof(sa_nl),
    .msg_iov = &iov, .msg_iovlen = 1,
    .msg_control = cred_buffer, .msg_controllen = sizeof(cred_buffer),
  };

  // read netlink message
  len = recvmsg(fd, &msg, 0);
  if (len == -1) {
    if (errno != EAGAIN && errno != EINTR)
      usbi_err("error receiving message from netlink, errno=%d", errno);
    return -1;
  }

  if (len < 32 || (msg.msg_flags & MSG_TRUNC)) {
    usbi_err("invalid netlink message length");
    return -1;
  }

  if (sa_nl.nl_groups != NL_GROUP_KERNEL || sa_nl.nl_pid != 0) {
    usbi_dbg("ignoring netlink message from unknown group/PID (%u/%u)",
      (unsigned int)sa_nl.nl_groups, (unsigned int)sa_nl.nl_pid);
    return -1;
  }

  cmsg = CMSG_FIRSTHDR(&msg);
  if (!cmsg || cmsg->cmsg_type != SCM_CREDENTIALS) {
    usbi_dbg("ignoring netlink message with no sender credentials");
    return -1;
  }

  cred = (struct ucred *)CMSG_DATA(cmsg);
  if (cred->uid != 0) {
    usbi_dbg("ignoring netlink message with non-zero sender UID %u", (unsigned int)cred->uid);
    return -1;
  }

  return linux_netlink_parse(
    msg_buffer,
    (size_t)len,
    ttyCtx,
    onInterfaceEnumerated,
    onUsbOff);
}

} // namespace

UsbEnumeratorNetlink::~UsbEnumeratorNetlink() {
  if (netlinkfd_ >= 0) {
    close(netlinkfd_);
  }

  if (eventfd_ >= 0) {
    close(eventfd_);
  }

  unload_driver();
}

void UsbEnumeratorNetlink::deleteWatch() noexcept {
  if (eventfd_ >= 0) {
    uint64_t dummy = 1;
    write(eventfd_, &dummy, sizeof(dummy));
  }
}

int UsbEnumeratorNetlink::createNetlink() {
  int socktype = SOCK_RAW | SOCK_NONBLOCK | SOCK_CLOEXEC;

  netlinkfd_ = -1;
  eventfd_ = eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
  if (eventfd_ == -1) {
    usbi_err("failed to create eventfd, errno=%d", errno);
    return -1;
  }

  int fd = socket(PF_NETLINK, socktype, NETLINK_KOBJECT_UEVENT);
  if (fd == -1 && errno == EINVAL) {
    usbi_dbg("failed to create netlink socket of type %d, attempting SOCK_RAW", socktype);
    socktype = SOCK_RAW;
    fd = socket(PF_NETLINK, socktype, NETLINK_KOBJECT_UEVENT);
  }

  int r = set_fd_cloexec_nb(fd, socktype);
  if (r == -1) {
    close(fd);
    return r;
  }

  struct sockaddr_nl sa_nl = { .nl_family = AF_NETLINK, .nl_groups = NL_GROUP_KERNEL };
  int opt = 1;

  r = bind(fd, (struct sockaddr *)&sa_nl, sizeof(sa_nl));
  if (r == -1) {
    close(fd);
    return r;
  }

  r = setsockopt(fd, SOL_SOCKET, SO_PASSCRED, &opt, sizeof(opt));
  if (r == -1) {
    close(fd);
    return r;
  }

  expect_tty_.timeout = 0;

  netlinkfd_ = fd;
  return fd;
}

void UsbEnumeratorNetlink::load_driver() {
  if (true) {
    char varg[8];
    char parg[8];
    sprintf(varg, "0x%04x", expect_tty_.vid);
    sprintf(parg, "0x%04x", expect_tty_.pid);
    process_lib::executeScriptNoOutput("rmmod {0} && modprobe {0} vendor={1} product={2} &", {"usbserial", varg, parg});
    driver_manually_loaded_ = true;
  }
}

void UsbEnumeratorNetlink::unload_driver() {
  if (driver_manually_loaded_) {
    process_lib::executeScriptNoOutput("rmmod usbserial &");
    driver_manually_loaded_ = false;
  }
}

bool UsbEnumeratorNetlink::poll(bool blocking) {
  if (netlinkfd_ < 0) {
    return false;
  }

  struct pollfd fds[] = {
  { .fd = eventfd_,
    .events = POLLIN },
  { .fd = netlinkfd_,
    .events = POLLIN },
  };

  int r = ::poll(fds, 2, blocking ? (expect_tty_.timeout > 0 ? expect_tty_.timeout : -1) : 0);

  if (expect_tty_.timeout > 0) {
    std::chrono::steady_clock::time_point new_time = std::chrono::steady_clock::now();
    int duration = (int)std::chrono::duration_cast<std::chrono::milliseconds>
            (new_time - expect_tty_.time).count();

    if (duration > expect_tty_.timeout) {
      if (expect_tty_.is_diag) {
        if (adb_booted_ > 1) {
          expect_tty_.timeout = 0;
          load_driver();
        } else {
          // diag port should come with adb port
          // delay till adb booted
          expect_tty_.time = new_time;
          expect_tty_.timeout = 1000;
        }
        if (adb_booted_) {
          adb_booted_++;
        }
      } else {
        expect_tty_.timeout = 0;
        load_driver();
      }
    }
  }

  if (r == -1) {
    // check for temporary failure
    if (errno == EINTR)
      return true;
    usbi_err("poll() failed, errno=%d", errno);
    return false;
  }

  if (r == 0) {
    return true;
  }

  if (fds[0].revents) {
    // activity on control event, exit
    return false;
  }

  if (fds[1].revents) {
    linux_netlink_read_message(
      netlinkfd_,
      expect_tty_,
      [this](const UsbInterfaceAttrs *attr) {
        sysfs_usb_interface_enumerated(attr);
      },
      [this](uint8_t busnum, uint8_t devaddr) {
        uint16_t session_id = ((uint16_t)busnum << 8) | devaddr;
        onInterfaceOff(std::to_string(session_id));
        adb_booted_ = 0;
        unload_driver();
      });
  }

  return true;
}

void UsbEnumeratorNetlink::enumerateDevices() {
  sysfs_get_device_list(expect_tty_, [this](const UsbInterfaceAttrs *attr) {
    sysfs_usb_interface_enumerated(attr);
  });
}

void UsbEnumeratorNetlink::sysfs_usb_interface_enumerated(const UsbInterfaceAttr* attr) {
  //printf("device %s\n", attr->tty.c_str());
  //printf("is_diag_port %d\n", attr->ifnum);

  if (attr->tty.size()) {
    DeviceState state;
    if (!is_edl_port(attr->vendor, attr->product, state) && !is_diag_port(attr->vendor, attr->product, attr->ifnum)) {
      if (attr->numinterfaces > 1 || attr->is_adb || attr->is_fastboot || attr->is_hdc) {
        // normal usb serial device cannot be a compisite device
        return;
      }
    }
  }

  DeviceNode newnode;
  newnode.hub = attr->identity;
  newnode.vid = attr->vendor;
  newnode.pid = attr->product;
  newnode.serial = attr->serial;

  uint16_t session_id = ((uint16_t)attr->busnum << 8) | attr->devaddr;
  auto interface_id = std::to_string(session_id);

  auto friendlyId = attr->identity;

  if (attr->tty.size()) {
    friendlyId = attr->tty;
    newnode.devpath = "/dev/" + attr->tty;
    newnode.description = attr->tty;
    // usb2serial
    newnode.type = DeviceState::Usb | DeviceState::ComPort;

    if (attr->numinterfaces == 1) {
      is_edl_port(newnode.vid, newnode.pid, newnode.type);
    }
  } else if (attr->is_adb || attr->is_fastboot || attr->is_hdc) {
    // ADB | Fastboot
    if (attr->is_adb) {
      newnode.type = DeviceState::Usb | DeviceState::Adb;
      newnode.description = "ADB - " + attr->identity;
    } else if (attr->is_fastboot) {
      newnode.type = DeviceState::Usb | DeviceState::Fastboot;
      newnode.description = "Fastboot - " + attr->identity;
    } else if (attr->is_hdc) {
      newnode.type = DeviceState::Usb | DeviceState::HDC;
      newnode.description = "HDC - " + attr->identity;
    }
  } else {
    return;
  }

  if (attr->productDesc.size()) {
    newnode.description = attr->productDesc + " (" + friendlyId + ")";
  }

  if (newnode.type & DeviceState::Adb) {
    adb_booted_ = 1;
  }
  this->onInterfaceEnumerated(interface_id, std::move(newnode));
  // printf("busnum %x devaddr %x vendor %x product %x [%s %s %s]\n", busnum, devaddr, vendor, product, newnode.description.c_str(), tty.c_str(), serial.c_str());
}

void UsbWatcherNetLink::createWatch(std::function<void(bool)> &&cb) noexcept {
  int fd = createNetlink();
  if (fd <= 0) {
    if (cb) cb(false);
    return;
  }

  initCallback_ = std::move(cb);

  if (settings_.enableModprobe && !process_lib::runingAsSudoer()) {
    settings_.enableModprobe = false;
  }

  initialEnumerateDevices();

  while (poll(true));

  deleteAdbTask();
}

} // namespace device_enumerator
