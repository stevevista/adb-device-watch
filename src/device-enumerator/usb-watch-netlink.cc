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

constexpr bool isUsb2SerialDevice(const std::vector<std::pair<uint16_t, uint16_t>> &usb2serialVidPid, uint16_t vid, uint16_t pid) {
  for (auto &pair : usb2serialVidPid) {
    if (pair.first == vid && (pair.second == pid || pair.second == 0)) {
      return true;
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
  int ifnum{-1};
  uint8_t usbClass{0};
  uint8_t usbSubClass{0};
  uint8_t usbProto{0};
};

namespace {

using UsbInterfaceAttrs = UsbEnumeratorNetlink::UsbInterfaceAttr;
using UsbSerialContext = UsbEnumeratorNetlink::UsbSerialContext;

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

  attr.usbClass = usb_class;
  attr.usbSubClass = usb_subclass;
  attr.usbProto = usb_protocol;

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
  int ifnum = 0;

  if (ifnums.size()) {
    ifnum = ifnums[0];
  }

    // a tty subssystem device should emerge
    // if not, we dynamiclly do mobprobe usbserial
  ttyCtx.timeout = timeout;
  ttyCtx.devpath = devpath;
  ttyCtx.vid = vid;
  ttyCtx.pid = pid;
  ttyCtx.ifnum = ifnum;
  ttyCtx.time = std::chrono::steady_clock::now();
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
    if (isUsb2SerialDevice(settings_.usb2serialVidPid, attr.vendor, attr.product)) {
      set_expect_tty_usbserial(
        ttyCtx,
        attr.vendor,
        attr.product,
        "",
        {unknownIfs},
        1); // expire immerately
    }
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

  if (isUsb2SerialDevice(settings_.usb2serialVidPid, vid, pid)) {
    set_expect_tty_usbserial(
        ttyCtx,
        vid,
        pid,
        devpath,
        {ifnum},
        1000);
  } else {
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

    return sysfs_get_usb_interface_adb(
      device_dir,
      attr,
      onInterfaceEnumerated);
  }

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
      expect_tty_.timeout = 0;
      load_driver();
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
        onUsbInterfaceOff(std::to_string(session_id));
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

  if (attr->tty.size()) {
    if (!isUsb2SerialDevice(settings_.usb2serialVidPid, attr->vendor, attr->product)) {
      // normal usb serial device cannot be a compisite device
      return;
    }
  }

  DeviceInterface newnode;
  newnode.hub = attr->identity;
  newnode.vid = attr->vendor;
  newnode.pid = attr->product;
  newnode.serial = attr->serial;
  newnode.usbIf = attr->ifnum;

  uint16_t session_id = ((uint16_t)attr->busnum << 8) | attr->devaddr;
  auto interface_id = std::to_string(session_id);

  auto friendlyId = attr->identity;

  if (attr->tty.size()) {
    friendlyId = attr->tty;
    newnode.devpath = "/dev/" + attr->tty;
    newnode.description = attr->tty;
    // usb2serial
    newnode.type = DeviceType::Usb | DeviceType::Serial;
  } else {
    newnode.type = DeviceType::Usb;
    newnode.description = "USB - " + attr->identity;
    newnode.usbClass = attr->usbClass;
    newnode.usbSubClass = attr->usbSubClass;
    newnode.usbProto = attr->usbProto;
  }

  if (attr->productDesc.size()) {
    newnode.description = attr->productDesc + " (" + friendlyId + ")";
  }

  this->onUsbInterfaceEnumerated(interface_id, std::move(newnode));
  // printf("busnum %x devaddr %x vendor %x product %x [%s %s %s]\n", busnum, devaddr, vendor, product, newnode.description.c_str(), tty.c_str(), serial.c_str());
}

void UsbWatcherNetLink::createWatch(std::function<void(bool)> &&cb) noexcept {
  int fd = createNetlink();
  if (fd <= 0) {
    if (cb) cb(false);
    return;
  }

  initCallback_ = std::move(cb);

  if (settings_.usb2serialVidPid.size() && !process_lib::runingAsSudoer()) {
    if (initCallback_) std::move(initCallback_)(false);
    return;
  }

  initialEnumerateDevices();

  while (poll(true));

  deleteAdbTask();
}

} // namespace device_enumerator
