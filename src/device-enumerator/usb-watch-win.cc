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

#ifdef ENABLE_TEST
#include <gtest/gtest.h>
#endif

#include "usb-watch-win.h"

#include <cctype>
#include <thread>
#include <regex>
#include <charconv>
#include <algorithm>
#include <chrono>
#include <iostream>
#include <Dbt.h>
#include <setupapi.h>
#include <Cfgmgr32.h>
#include <initguid.h>
#include <Usbiodef.h>
#include <devioctl.h>
#include <Usbioctl.h>
#include <combaseapi.h>
#include <usbscan.h>
#include <winusb.h>

#include "adb-client.h"

#pragma comment(lib,"setupapi.lib") 
#pragma comment(lib, "Cfgmgr32.lib")
#pragma comment(lib, "Winusb.lib")

namespace device_enumerator {

namespace {

constexpr std::string_view kQCOMDiagDriver = "qcusbser";
constexpr std::string_view kWinUSBDriver = "WinUSB";


std::string toAscii(const wchar_t *wfirst, const wchar_t *wlast) {
	int destLen = ::WideCharToMultiByte(CP_ACP, 0, wfirst, static_cast<int>(wlast - wfirst), nullptr, 0, nullptr, nullptr);  
  if (destLen < 0) return std::string();

  std::string result;
  result.resize(destLen);
   
	::WideCharToMultiByte(CP_ACP, 0, wfirst, static_cast<int>(wlast - wfirst), &result[0], destLen+1, nullptr, nullptr); 
	result[destLen] = 0;
	return result;
}

// Qualcoom 0x05c6
// JLQ 0x31ef

constexpr GUID GUID_DEVINTERFACE_COMPORT = { 0x86e0d1e0, 0x8089, 0x11d0,
					  0x9c, 0xe4, 0x08, 0x00, 0x3e, 0x30, 0x1f, 0x73 };

constexpr GUID GUID_DEVINTERFACE_ADB = {0xf72fe0d4, 0xcbcb, 0x407d, {0x88, 0x14, 0x9e, 0xd6, 0x73, 0xd0, 0xdd, 0x6b}};

constexpr GUID kUsbGuidClasses[] = {
  GUID_DEVINTERFACE_COMPORT,
  GUID_DEVINTERFACE_ADB,
};
constexpr size_t kUsbGuidClassesCount = sizeof(kUsbGuidClasses) / sizeof(kUsbGuidClasses[0]);

const std::regex re_usbvidpid("#vid_([0-9a-f]+)&pid_([0-9a-f]+)[#|&]", std::regex::icase);
const std::wregex re_comport(L".*\\((COM\\d+)\\)$");

#define MAX_USB_PATH ((8 * 3) + (7 * 1) + 1)

DWORD GetAddress(const DEVINST Inst) {
  DWORD address = 0;
  DWORD size = sizeof(address);

  if (CR_SUCCESS == CM_Get_DevNode_Registry_PropertyA(
      Inst,
      CM_DRP_ADDRESS,
      0,
      &address,
      &size,
      0)) {
    return address;
  }

  return (DWORD)-1;
}

bool IsCompositeUSBNode(const DEVINST Inst) {
  char compat_ids[512] = {0};
  DWORD size = sizeof(compat_ids);

  if (CR_SUCCESS == CM_Get_DevNode_Registry_PropertyA(
    Inst,
    CM_DRP_COMPATIBLEIDS,
    0,
    &compat_ids,
    &size,
    0)) {
    char *p = compat_ids;
    while (*p) {
      if (!strncmp(p, "USB\\COMPOSITE", 13))
        return true;
      p += strlen(p) + 1;
    }
  }

  return false;
}

std::string setupDiGetServiceType(HDEVINFO hDevInfo, PSP_DEVINFO_DATA pDeviInfoData) {
  char service[64];
  DWORD reg_type;
  DWORD size;
  if (SetupDiGetDeviceRegistryPropertyA(hDevInfo, pDeviInfoData, SPDRP_SERVICE,
			&reg_type, (PBYTE)service, sizeof(service), &size)) {
    return service;
  }

  return std::string{};
}

std::string setupDiGetDevpath(HDEVINFO hDevInfo, PSP_INTERFACE_DEVICE_DATA sdid, PSP_DEVINFO_DATA sdd) {
  BYTE buffer[256] = {0};
  auto deviceInterfaceDetailData = (PSP_INTERFACE_DEVICE_DETAIL_DATA_A)buffer;
  deviceInterfaceDetailData->cbSize = sizeof (SP_INTERFACE_DEVICE_DETAIL_DATA_A);
  DWORD requiredLength;

  if (SetupDiGetDeviceInterfaceDetailA(
        hDevInfo,
        sdid,
        deviceInterfaceDetailData,
        sizeof(buffer),
        &requiredLength,
        sdd)) {
    return std::string(deviceInterfaceDetailData->DevicePath);
  }

  return "";
}

HDEVINFO setupDiGetDeviceInterfaceByDevId(
  const GUID *InterfaceClassGuid,
  PCSTR DeviceInstanceId,
  PSP_DEVINFO_DATA DeviceInfoData,
  PSP_DEVICE_INTERFACE_DATA DeviceInterfaceData) {

  HDEVINFO hDeviceInfo = SetupDiGetClassDevs(
			InterfaceClassGuid,
			nullptr,
			nullptr,
			DIGCF_PRESENT | DIGCF_DEVICEINTERFACE
		);

	if( hDeviceInfo == INVALID_HANDLE_VALUE ) {
		return hDeviceInfo;
	}

  DeviceInfoData->cbSize = sizeof(SP_DEVINFO_DATA);

  if (!SetupDiOpenDeviceInfoA(hDeviceInfo, DeviceInstanceId, nullptr, 0, DeviceInfoData)) {
    SetupDiDestroyDeviceInfoList(hDeviceInfo);
    return INVALID_HANDLE_VALUE;
  }

  if (DeviceInterfaceData) {
    DeviceInterfaceData->cbSize = sizeof(SP_DEVICE_INTERFACE_DATA);
    if (!SetupDiEnumDeviceInterfaces(hDeviceInfo, DeviceInfoData, InterfaceClassGuid, 0, DeviceInterfaceData)) {
      SetupDiDestroyDeviceInfoList(hDeviceInfo);
      return INVALID_HANDLE_VALUE;
    }
  }

  return hDeviceInfo;
}

std::string setupDiGetDevpathByDevId(const GUID *InterfaceClassGuid, PCSTR DeviceInstanceId) {
  SP_DEVICE_INTERFACE_DATA sdid;
  SP_DEVINFO_DATA sdd;
  
  auto hDeviceInfo = setupDiGetDeviceInterfaceByDevId(
      InterfaceClassGuid,
      DeviceInstanceId,
      &sdd,
      &sdid);

	if(hDeviceInfo != INVALID_HANDLE_VALUE) {
    auto devpath = setupDiGetDevpath(hDeviceInfo, &sdid, nullptr);
    SetupDiDestroyDeviceInfoList(hDeviceInfo);
		return devpath;
	}

  return "";
}

std::string TransformDevpathToDevId(const std::string &devpath) {
  // "\\\\?\\usb#vid_31ef&pid_9091&mi_03#6&897122b&0&0003#{f72fe0d4-cbcb-407d-8814-9ed673d0dd6b}"
  // ==> USB\\VID_31EF&PID_9091&MI_03\\6&897122B&0&0003

  // strip off leading 4 bytes
  const char *begin = &devpath[4];
  const char *end = begin + devpath.size() - 4;

  // strip off tail classguid
  auto pos = devpath.rfind("#{");
  if (pos != std::string::npos) {
    end = &devpath[pos];
  }

  size_t idlen = end - begin;
  std::string devid(idlen, 0);

  // transfer to upper & replace # with '\'
  std::transform(begin, end, devid.begin(), [](char c) -> char { 
    if (c == '#') return '\\';
    return std::toupper(c); 
  });

  return devid;
}

#define LIBUSB_ENDPOINT_IN 0x80
#define LIBUSB_REQUEST_GET_DESCRIPTOR 0x06
#define LIBUSB_DT_CONFIG 0x02
#define LIBUSB_DT_INTERFACE 0x04
#define LIBUSB_DT_ENDPOINT 0x05
#define LIBUSB_DT_DEVICE 0x01

#define LIBUSB_ERROR_IO -1
#define USB_MAXINTERFACES	32
#define USB_MAXENDPOINTS 32


typedef struct {
	UCHAR  bLength;
	UCHAR  bDescriptorType;
} USBI_DESCRIPTOR_HEADER, *PUSBI_DESCRIPTOR_HEADER;

using UsbInterfaceConfig = std::vector<USB_INTERFACE_DESCRIPTOR>;

struct UsbConfigDescriptor {
  USB_CONFIGURATION_DESCRIPTOR header;
  std::vector<UsbInterfaceConfig> interfaces;
};

int skipEndpoint(const uint8_t *buffer, int size) {
	int parsed = 0;

	if (size < sizeof(USBI_DESCRIPTOR_HEADER)) {
		return LIBUSB_ERROR_IO;
	}

	auto header = (const PUSBI_DESCRIPTOR_HEADER)buffer;
	if (header->bDescriptorType != LIBUSB_DT_ENDPOINT) {
		return parsed;
	} else if (header->bLength < sizeof(USB_ENDPOINT_DESCRIPTOR)) {
		return LIBUSB_ERROR_IO;
	} else if (header->bLength > size) {
		return parsed;
	}

	buffer += header->bLength;
	size -= header->bLength;
	parsed += header->bLength;

	/* Skip over the rest of the Class Specific or Vendor Specific */
	/*  descriptors */
	while (size >= sizeof(USBI_DESCRIPTOR_HEADER)) {
		header = (const PUSBI_DESCRIPTOR_HEADER)buffer;
		if (header->bLength < sizeof(USBI_DESCRIPTOR_HEADER)) {
			return LIBUSB_ERROR_IO;
		} else if (header->bLength > size) {
			return parsed;
		}

		/* If we find another "proper" descriptor then we're done  */
		if (header->bDescriptorType == LIBUSB_DT_ENDPOINT ||
		    header->bDescriptorType == LIBUSB_DT_INTERFACE ||
		    header->bDescriptorType == LIBUSB_DT_CONFIG ||
		    header->bDescriptorType == LIBUSB_DT_DEVICE)
			break;

		buffer += header->bLength;
		size -= header->bLength;
		parsed += header->bLength;
	}

	return parsed;
}

int parseInterface(
	UsbInterfaceConfig &usb_interface, const uint8_t *buffer, int size) {
	int parsed = 0;
	int interface_number = -1;

	while (size >= sizeof(USB_INTERFACE_DESCRIPTOR)) {
    USB_INTERFACE_DESCRIPTOR ifp;

    memcpy(&ifp, buffer, sizeof(USB_INTERFACE_DESCRIPTOR));

		if (ifp.bDescriptorType != LIBUSB_DT_INTERFACE) {
			return parsed;
		} else if (ifp.bLength < sizeof(USB_INTERFACE_DESCRIPTOR)) {
			return LIBUSB_ERROR_IO;
		} else if (ifp.bLength > size) {
			return parsed;
		} else if (ifp.bNumEndpoints > USB_MAXENDPOINTS) {
			return LIBUSB_ERROR_IO;
		}

		if (interface_number == -1)
			interface_number = ifp.bInterfaceNumber;

		/* Skip over the interface */
		buffer += ifp.bLength;
		parsed += ifp.bLength;
		size -= ifp.bLength;

    /* Skip over any interface, class or vendor descriptors */
		while (size >= sizeof(USBI_DESCRIPTOR_HEADER)) {
      auto *header = (const PUSBI_DESCRIPTOR_HEADER)buffer;
			if (header->bLength < sizeof(USBI_DESCRIPTOR_HEADER)) {
				return LIBUSB_ERROR_IO;
			} else if (header->bLength > size) {
				return parsed;
			}

			/* If we find another "proper" descriptor then we're done */
			if (header->bDescriptorType == LIBUSB_DT_INTERFACE ||
			    header->bDescriptorType == LIBUSB_DT_ENDPOINT ||
			    header->bDescriptorType == LIBUSB_DT_CONFIG ||
			    header->bDescriptorType == LIBUSB_DT_DEVICE)
				break;

			buffer += header->bLength;
			parsed += header->bLength;
			size -= header->bLength;
    }

    if (ifp.bNumEndpoints > 0) {
			for (uint8_t i = 0; i < ifp.bNumEndpoints; i++) {
				int r = skipEndpoint(buffer, size);
				if (r < 0) {
					return r;
        }
				if (r == 0) {
					break;
				}

				buffer += r;
				parsed += r;
				size -= r;
			}
		}

    usb_interface.push_back(ifp);

    /* We check to see if it's an alternate to this one */
		auto *if_desc = (PUSB_INTERFACE_DESCRIPTOR)buffer;
		if (size < sizeof(USB_INTERFACE_DESCRIPTOR) ||
		    if_desc->bDescriptorType != LIBUSB_DT_INTERFACE ||
		    if_desc->bInterfaceNumber != interface_number)
			return parsed;
  }

  return 0;
}

int parseConfiguration(UsbConfigDescriptor &config, const uint8_t *buffer, int size)
{
  memcpy(&config.header, buffer, sizeof(USB_CONFIGURATION_DESCRIPTOR));

  if (config.header.bDescriptorType != LIBUSB_DT_CONFIG) {
		return LIBUSB_ERROR_IO;
	} else if (config.header.bLength < sizeof(USB_CONFIGURATION_DESCRIPTOR)) {
		return LIBUSB_ERROR_IO;
	} else if (config.header.bLength > size) {
		return LIBUSB_ERROR_IO;
	} else if (config.header.bNumInterfaces > USB_MAXINTERFACES) {
		return LIBUSB_ERROR_IO;
	}

  buffer += config.header.bLength;
	size -= config.header.bLength;

  int r = 0;

  for (int i = 0; i < config.header.bNumInterfaces; i++) {
		/* Skip over the rest of the Class Specific or Vendor */
		/*  Specific descriptors */
		while (size >= sizeof(USBI_DESCRIPTOR_HEADER)) {
      auto header = (const PUSBI_DESCRIPTOR_HEADER)buffer;
			if (header->bLength < sizeof(USBI_DESCRIPTOR_HEADER)) {
				return LIBUSB_ERROR_IO;
			} else if (header->bLength > size) {
				config.header.bNumInterfaces = i;
				return size;
			}

      /* If we find another "proper" descriptor then we're done */
			if (header->bDescriptorType == LIBUSB_DT_ENDPOINT ||
			    header->bDescriptorType == LIBUSB_DT_INTERFACE ||
			    header->bDescriptorType == LIBUSB_DT_CONFIG ||
			    header->bDescriptorType == LIBUSB_DT_DEVICE)
				break;

			buffer += header->bLength;
			size -= header->bLength;
    }

    UsbInterfaceConfig usb_interface;
    r = parseInterface(usb_interface, buffer, size);
		if (r < 0) {
			return r;
    }
		if (r == 0) {
			break;
		}

    config.interfaces.push_back(usb_interface);

		buffer += r;
		size -= r;
  }

  return 0;
}

std::string queryUsbStringDescriptor(HANDLE hHubHandle, DWORD port, UCHAR iIndex, USHORT LanguageID) {
  std::string ret;

  ULONG nBytes = 0;
  ULONG nBytesReturned = 0;

  if (LanguageID == 0) {
    // get language id
    nBytesReturned = 0;
    UCHAR  stringDescReqBuf[sizeof(USB_DESCRIPTOR_REQUEST) +
                              MAXIMUM_USB_STRING_LENGTH];
    PUSB_DESCRIPTOR_REQUEST stringDescReq = nullptr;
    PUSB_STRING_DESCRIPTOR stringDesc = nullptr;

    nBytes = sizeof(stringDescReqBuf);

    stringDescReq = (PUSB_DESCRIPTOR_REQUEST)stringDescReqBuf;
    stringDesc = (PUSB_STRING_DESCRIPTOR)(stringDescReq+1);

    memset(stringDescReq, 0, nBytes);

    // Indicate the port from which the descriptor will be requested
    //
    stringDescReq->ConnectionIndex = port;

    stringDescReq->SetupPacket.wValue = (USB_STRING_DESCRIPTOR_TYPE << 8);
    stringDescReq->SetupPacket.wIndex = 0;
    stringDescReq->SetupPacket.wLength = (USHORT)(nBytes - sizeof(USB_DESCRIPTOR_REQUEST));

    if (DeviceIoControl(hHubHandle,
              IOCTL_USB_GET_DESCRIPTOR_FROM_NODE_CONNECTION,
              stringDescReq,
              nBytes,
              stringDescReq,
              nBytes,
              &nBytesReturned,
              nullptr)) {
      LanguageID = (USHORT)stringDesc->bString[0];
    }
  }

  nBytesReturned = 0;
  UCHAR  stringDescReqBuf[sizeof(USB_DESCRIPTOR_REQUEST) +
                              MAXIMUM_USB_STRING_LENGTH];
  PUSB_DESCRIPTOR_REQUEST stringDescReq = nullptr;
  PUSB_STRING_DESCRIPTOR stringDesc = nullptr;

  nBytes = sizeof(stringDescReqBuf);

  stringDescReq = (PUSB_DESCRIPTOR_REQUEST)stringDescReqBuf;
  stringDesc = (PUSB_STRING_DESCRIPTOR)(stringDescReq+1);

  memset(stringDescReq, 0, nBytes);

      // Indicate the port from which the descriptor will be requested
      //
  stringDescReq->ConnectionIndex = port;

      //
      // USBHUB uses URB_FUNCTION_GET_DESCRIPTOR_FROM_DEVICE to process this
      // IOCTL_USB_GET_DESCRIPTOR_FROM_NODE_CONNECTION request.
      //
      // USBD will automatically initialize these fields:
      //     bmRequest = 0x80
      //     bRequest  = 0x06
      //
      // We must inititialize these fields:
      //     wValue    = Descriptor Type (high) and Descriptor Index (low byte)
      //     wIndex    = Zero (or Language ID for String Descriptors)
      //     wLength   = Length of descriptor buffer
      //
  stringDescReq->SetupPacket.wValue = (USB_STRING_DESCRIPTOR_TYPE << 8) | iIndex;
  stringDescReq->SetupPacket.wIndex = LanguageID;
  stringDescReq->SetupPacket.wLength = (USHORT)(nBytes - sizeof(USB_DESCRIPTOR_REQUEST));

  if (DeviceIoControl(hHubHandle,
              IOCTL_USB_GET_DESCRIPTOR_FROM_NODE_CONNECTION,
              stringDescReq,
              nBytes,
              stringDescReq,
              nBytes,
              &nBytesReturned,
              nullptr)) {
    ret = toAscii(stringDesc->bString, stringDesc->bString + std::wcslen(stringDesc->bString));
  }

  return ret;
}

void queryUsbProperties(const char *interface_name, const char *hub_device_id, ULONG connectionIndex, int interfaceNumber, DeviceNode &newdev) {
  auto path = setupDiGetDevpathByDevId(&GUID_DEVINTERFACE_USB_HUB, hub_device_id);
  HANDLE hub_handle = CreateFileA(path.c_str(), GENERIC_WRITE, FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);

  if (hub_handle == INVALID_HANDLE_VALUE) {
    return;
  }

  USB_NODE_CONNECTION_INFORMATION_EX conn_info{0};

  conn_info.ConnectionIndex = connectionIndex;
  DWORD size;

  if (DeviceIoControl(hub_handle, IOCTL_USB_GET_NODE_CONNECTION_INFORMATION_EX, &conn_info, sizeof(conn_info),
				&conn_info, sizeof(conn_info), &size, NULL)) {
		if (conn_info.ConnectionStatus != NoDeviceConnected) {

      UCHAR  cd_buf_short[sizeof(USB_DESCRIPTOR_REQUEST) +
                              sizeof(USB_CONFIGURATION_DESCRIPTOR)]; // dummy request
      PUSB_DESCRIPTOR_REQUEST cd_buf_actual = NULL;    // actual request
      PUSB_CONFIGURATION_DESCRIPTOR cd_data;

      UCHAR activeConfig = conn_info.CurrentConfigurationValue;

      newdev.vid = conn_info.DeviceDescriptor.idVendor;
      newdev.pid = conn_info.DeviceDescriptor.idProduct;

      for (UCHAR i = 0; i < conn_info.DeviceDescriptor.bNumConfigurations; i++) {

        memset(&cd_buf_short, 0, sizeof(cd_buf_short));

        auto req = (PUSB_DESCRIPTOR_REQUEST)cd_buf_short;
        auto desc = (PUSB_CONFIGURATION_DESCRIPTOR)(&cd_buf_short[sizeof(USB_DESCRIPTOR_REQUEST)]);
        DWORD ret_size = 0;

        size = sizeof(cd_buf_short);
        req->ConnectionIndex = connectionIndex;
        req->SetupPacket.bmRequest = LIBUSB_ENDPOINT_IN;
        req->SetupPacket.bRequest = LIBUSB_REQUEST_GET_DESCRIPTOR;
        req->SetupPacket.wValue = (LIBUSB_DT_CONFIG << 8) | i;
        req->SetupPacket.wIndex = 0;
        req->SetupPacket.wLength = (USHORT)sizeof(USB_CONFIGURATION_DESCRIPTOR);

        // Dummy call to get the required data size. Initial failures are reported as info rather
        // than error as they can occur for non-penalizing situations, such as with some hubs.
        // coverity[tainted_data_argument]
        if (!DeviceIoControl(hub_handle, IOCTL_USB_GET_DESCRIPTOR_FROM_NODE_CONNECTION, req, size,
          req, size, &ret_size, NULL)) {
          continue;
        }

        if ((ret_size != sizeof(cd_buf_short)) || (desc->wTotalLength < sizeof(USB_CONFIGURATION_DESCRIPTOR))) {
          continue;
        }

        DWORD size = sizeof(USB_DESCRIPTOR_REQUEST) + desc->wTotalLength;
        std::unique_ptr<char[]> cd_buf_actual(new char[size]);

        // Actual call
        req = (PUSB_DESCRIPTOR_REQUEST)cd_buf_actual.get();
        req->ConnectionIndex = connectionIndex;
        req->SetupPacket.bmRequest = LIBUSB_ENDPOINT_IN;
        req->SetupPacket.bRequest = LIBUSB_REQUEST_GET_DESCRIPTOR;
        req->SetupPacket.wValue = (LIBUSB_DT_CONFIG << 8) | i;
        req->SetupPacket.wIndex = 0;
        req->SetupPacket.wLength = desc->wTotalLength;

        if (!DeviceIoControl(hub_handle, IOCTL_USB_GET_DESCRIPTOR_FROM_NODE_CONNECTION, req, size,
          req, size, &ret_size, NULL)) {
          continue;
        }

        cd_data = (PUSB_CONFIGURATION_DESCRIPTOR)((UCHAR *)cd_buf_actual.get() + sizeof(USB_DESCRIPTOR_REQUEST));

        if ((size != ret_size) || (cd_data->wTotalLength != desc->wTotalLength)) {
          continue;
        }

        if (cd_data->bDescriptorType != LIBUSB_DT_CONFIG) {
          continue;
        }

        UsbConfigDescriptor config;
        if (size > desc->wTotalLength) {
          size = desc->wTotalLength;
        }
        int r = parseConfiguration(config, (const uint8_t *)cd_data, size);

        if (r >= 0 && (activeConfig == 0 || config.header.bConfigurationValue == activeConfig)) {
          int iIndex = interfaceNumber;
        
          if (iIndex < 0) {
            iIndex = 0;
          }

          if (iIndex < config.interfaces.size()) {
            if (config.interfaces[iIndex].size() > 0) {
              auto ifClass = config.interfaces[iIndex][0].bInterfaceClass;
              auto ifSubClass = config.interfaces[iIndex][0].bInterfaceSubClass;
              auto ifProto = config.interfaces[iIndex][0].bInterfaceProtocol;

              if (ifClass == 0xff) {
                if (ifSubClass == 0x50 && ifProto == 0x01) {
                  newdev.type |= DeviceState::HDC;
                } else if (ifSubClass == 0x42 && ifProto == 0x01) {
                  newdev.type |= DeviceState::Adb;

                } else if (ifSubClass == 0x42 && ifProto == 0x03) {
                  newdev.type |= DeviceState::Fastboot;
                }
              }
            }
          }

          newdev.serial = queryUsbStringDescriptor(hub_handle, connectionIndex, conn_info.DeviceDescriptor.iSerialNumber, 0x409);
          newdev.manufacturer = queryUsbStringDescriptor(hub_handle, connectionIndex, conn_info.DeviceDescriptor.iManufacturer, 0x409);
          newdev.product = queryUsbStringDescriptor(hub_handle, connectionIndex, conn_info.DeviceDescriptor.iProduct, 0x409);
    
          break;
        }
      }
		}
	}

  CloseHandle(hub_handle);
}

bool queryUsbUniquePath(
  DEVINST dev_inst,
  std::string &uniquePath,
  std::string &hubDeviceId,
  DWORD &hubPort
) {
  char device_id[MAX_DEVICE_ID_LEN];
  char usb_path[MAX_USB_PATH] = "", tmp[MAX_USB_PATH];

  DEVINST hub_devinst;

  if (CM_Get_Parent(&hub_devinst, dev_inst, 0) != CR_SUCCESS) {
    return false;
  }

  if (CM_Get_Device_IDA(hub_devinst, device_id, sizeof(device_id), 0) != CR_SUCCESS) {
    return false;
  }

  hubDeviceId = device_id;

  DWORD address = GetAddress(dev_inst);
  DWORD hub_address = GetAddress(hub_devinst);

  hubPort = address;

  bool isRoot = !strncmp(device_id, "USB\\ROOT", 8);
  if (isRoot) {
    hub_address++;
  }

  snprintf(usb_path, sizeof(usb_path), "USB%d-%d",
                    (int)hub_address, (int)hubPort);

  DEVINST node = hub_devinst;

  for (; !isRoot; ) {
    if (CM_Get_Parent(&node, node, 0) != CR_SUCCESS) {
      break;
    }

    // Verify that this layer of the tree is USB related.
    if (CM_Get_Device_IDA(node, device_id, sizeof(device_id), 0) != CR_SUCCESS
      || strncmp(device_id, "USB\\", 4))
			continue;

    isRoot = !strncmp(device_id, "USB\\ROOT", 8);

    DWORD address = GetAddress(node);
    if (address != (DWORD)-1) {
      // increase root number
      // linux is from 1 (windows from zero)
      // so make it compitable with windows
      if (isRoot) {
        address++;
      }

      strncpy_s(tmp, usb_path, MAX_USB_PATH);
      snprintf(usb_path, sizeof(usb_path), "%d-%s",
                    (int)address, tmp);
    }
  }

  if (isRoot) {
    uniquePath = usb_path;
  }

  return true;
}

bool setupDiGetInterfaceDescritption(
  HDEVINFO hDeviceInfo,
  const PSP_DEVINFO_DATA deviceInfoData,
  std::wstring &description,
  std::string &comport
) {
  wchar_t wfriendlyname[MAX_PATH] = {0};

  SetupDiGetDeviceRegistryPropertyW(
      hDeviceInfo, 
      deviceInfoData,
      SPDRP_FRIENDLYNAME,
      nullptr,
      (PBYTE)wfriendlyname, 
      sizeof(wfriendlyname),
      nullptr);

  if (wfriendlyname[0]) {
    // use COMXXX instead of underlying devpath
    std::match_results<const wchar_t*> m;
    if(std::regex_match(wfriendlyname, m, re_comport)) {
      comport = toAscii(m[1].first, m[1].second);
    }

    description = wfriendlyname;
  }

  return true;
}

} // namespace

void UsbEnumeratorWindows::notifyInterfaceRemove(const GUID *guid, const std::string &devpath) {
  onUsbInterfaceOff(devpath);
}

void UsbEnumeratorWindows::handleUsbInterfaceEnumated(
  const GUID *guid,
  HDEVINFO hDeviceInfo,
  const PSP_DEVINFO_DATA deviceInfoData,
  const std::string &interfaceDevpath
) {
  DeviceNode newdev;
  newdev.driver = setupDiGetServiceType(hDeviceInfo, deviceInfoData);

  std::string comport;
  std::string usbPortPath;
  std::string hubDeviceId;
  DWORD hubPort;
  int interfaceNumber = -1;

  DEVINST dev_inst = deviceInfoData->DevInst;

  const char *mi_str = strstr(interfaceDevpath.c_str(), "&mi_");
  if ((mi_str != NULL) && isdigit((unsigned char)mi_str[4]) && isdigit((unsigned char)mi_str[5])) {
    interfaceNumber = ((mi_str[4] - '0') * 10) + (mi_str[5] - '0');

      // this is a interface of compsite device
    if (CM_Get_Parent(&dev_inst, dev_inst, 0) != CR_SUCCESS) {
      return;
    }
  }

  if (!queryUsbUniquePath(dev_inst,
          usbPortPath, hubDeviceId, hubPort)) {
    return;
  }

  if (usbPortPath.size()) {
    newdev.type |= DeviceState::Usb;

    // parse VID/PID from path string 
    std::match_results<const char*> m;
    if(std::regex_search(interfaceDevpath.data(), m, re_usbvidpid)) {
      std::from_chars(m[1].first, m[1].second, newdev.vid, 16);
      std::from_chars(m[2].first, m[2].second, newdev.pid, 16);
    }

    queryUsbProperties(interfaceDevpath.c_str(), hubDeviceId.c_str(), hubPort, interfaceNumber, newdev);
  }

  newdev.hub = std::move(usbPortPath);

  if (!setupDiGetInterfaceDescritption(
    hDeviceInfo,
    deviceInfoData,
    newdev.description,
    comport
  )) {
    return;
  }

  if (IsEqualGUID(*guid, GUID_DEVINTERFACE_COMPORT)) {
    if (comport.size()) {
      newdev.devpath = std::move(comport);
    } else {
      newdev.devpath = interfaceDevpath;
    }

    newdev.type |= DeviceState::ComPort;
  } else {
    if (newdev.driver == kQCOMDiagDriver) {
      newdev.type |= DeviceState::Diag;
    }
  }

  onUsbInterfaceEnumerated(interfaceDevpath, std::move(newdev));
}

void UsbEnumeratorWindows::enumerateUsbInterfaces(
    const GUID *guid,
    const std::string &expect_devpath) {
  auto devid = TransformDevpathToDevId(expect_devpath);

  HDEVINFO hDeviceInfo = SetupDiGetClassDevs(
			guid,
			nullptr,
			nullptr,
			DIGCF_PRESENT | DIGCF_DEVICEINTERFACE
		);

	if( hDeviceInfo == INVALID_HANDLE_VALUE) {
		return;
	}

  SP_DEVINFO_DATA sdd;
  sdd.cbSize = sizeof(SP_DEVINFO_DATA);

  if (SetupDiOpenDeviceInfoA(hDeviceInfo, devid.c_str(), nullptr, 0, &sdd)) {
    handleUsbInterfaceEnumated(guid, hDeviceInfo, &sdd, expect_devpath);
  }

  SetupDiDestroyDeviceInfoList(hDeviceInfo);
}

void UsbEnumeratorWindows::enumerateUsbInterfaces(const GUID *guid) {
  HDEVINFO dev_info = SetupDiGetClassDevsA(guid, nullptr, nullptr, DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);
	if (dev_info == INVALID_HANDLE_VALUE) {
    return;
	}

  SP_DEVINFO_DATA dev_info_data;
  SP_DEVICE_INTERFACE_DATA dev_interface_data;

  dev_info_data.cbSize = sizeof(SP_DEVINFO_DATA);
  dev_interface_data.cbSize = sizeof(SP_DEVICE_INTERFACE_DATA);

  DWORD index = 0;

  for (;;) {
    if (!SetupDiEnumDeviceInfo(dev_info, index++, &dev_info_data)) {
      break;
    }

    if (!SetupDiEnumDeviceInterfaces(dev_info, &dev_info_data, guid, 0, &dev_interface_data)) {
      continue;
    }

    auto devpath = setupDiGetDevpath(dev_info, &dev_interface_data, nullptr);
    if (devpath.empty()) {
      continue;
    }

    handleUsbInterfaceEnumated(guid, dev_info, &dev_info_data, devpath);
  }

  SetupDiDestroyDeviceInfoList(dev_info);
}

void UsbEnumeratorWindows::notifyInterfaceArrival(const GUID *guid, const std::string &unique_devpath) {
  enumerateUsbInterfaces(
    guid,
    unique_devpath);
}

void UsbEnumeratorWindows::enumerateDevices() {
  for (size_t i = 0; i < kUsbGuidClassesCount; i++) {
    enumerateUsbInterfaces(&kUsbGuidClasses[i]);
  }
}

void UsbEnumeratorWindows::onWatchDestroy() {
  if (this->hwnd_) {
    this->hwnd_ = nullptr;
  }

  deleteAdbTask();
}

void UsbEnumeratorWindows::onWatchCreate(HWND hWnd) {
  if (this->hwnd_) {
    // already created
    return;
  }

  this->hwnd_ = hWnd;

  {
    DEV_BROADCAST_DEVICEINTERFACE_A NotificationFilter;
    ZeroMemory(&NotificationFilter, sizeof(NotificationFilter));
    NotificationFilter.dbcc_size = sizeof(DEV_BROADCAST_DEVICEINTERFACE_A);
    NotificationFilter.dbcc_devicetype = DBT_DEVTYP_DEVICEINTERFACE;

    HDEVNOTIFY hDeviceNotify = RegisterDeviceNotificationA(
        hWnd,                       // events recipient
        &NotificationFilter,        // type of device
        DEVICE_NOTIFY_WINDOW_HANDLE | DEVICE_NOTIFY_ALL_INTERFACE_CLASSES // type of recipient handle
    );
  }

  initialEnumerateDevices();
}

namespace {

template <typename T>
inline size_t xstrlen(const T*);

template <>
inline size_t xstrlen<char>(const char *s) { return strlen(s); }
template <>
inline size_t xstrlen<wchar_t>(const wchar_t *s) { return std::wcslen(s); }

}

template<typename T>
void UsbEnumeratorWindows::OnDeviceChangeX(HWND hwnd, WPARAM wParam, LPARAM lParam) {
  if (wParam == DBT_DEVICEARRIVAL || wParam == DBT_DEVICEREMOVECOMPLETE) {
    using DEV_BROADCAST_DEVICEINTERFACE_X = 
        std::conditional_t<sizeof (T) == 1, DEV_BROADCAST_DEVICEINTERFACE_A, DEV_BROADCAST_DEVICEINTERFACE_W>;
    auto b = reinterpret_cast<DEV_BROADCAST_DEVICEINTERFACE_X*>(lParam);
    if (b && b->dbcc_devicetype == DBT_DEVTYP_DEVICEINTERFACE && b->dbcc_name[0] == (T)'\\') {
      bool matched = false;
      for (size_t i = 0; i < kUsbGuidClassesCount; i++) {
        if (IsEqualGUID(b->dbcc_classguid, kUsbGuidClasses[i])) {
          matched = true;
          break;
        }
      }

      if (!matched) {
        return;
      }

      size_t namelen = xstrlen(b->dbcc_name);
      std::string devpath(namelen, 0);
      std::transform(b->dbcc_name, b->dbcc_name + namelen, devpath.begin(), [](T c) -> char { return std::tolower((char)c); });

      if (wParam == DBT_DEVICEARRIVAL) {
        notifyInterfaceArrival(&b->dbcc_classguid, devpath);
      } else {
        notifyInterfaceRemove(&b->dbcc_classguid, devpath);
      }
    }
  }
}

template<typename T>
bool UsbEnumeratorWindows::OnDeviceWatchMessageHandleX(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
  if (uMsg == WM_DEVICECHANGE) {
    OnDeviceChangeX<T>(hwnd, wParam, lParam);
    return true;
  }

  if (uMsg == WM_CREATE || uMsg == WM_INITDIALOG) {
    this->onWatchCreate(hwnd);
    return false;
  }

  if (uMsg == WM_DESTROY) {
    onWatchDestroy();
  }

  return false;
}

bool UsbEnumeratorWindows::OnDeviceWatchMessageHandleA(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
  return OnDeviceWatchMessageHandleX<char>(hwnd, uMsg, wParam, lParam);
}

bool UsbEnumeratorWindows::OnDeviceWatchMessageHandleW(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
  return OnDeviceWatchMessageHandleX<wchar_t>(hwnd, uMsg, wParam, lParam);
}

LRESULT CALLBACK UsbWatcherWindows::WndProcA(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam) {
  UsbWatcherWindows* self = nullptr;
  if (message != WM_NCCREATE) {
    self = reinterpret_cast<UsbWatcherWindows*>(GetWindowLongPtrA(hWnd, GWLP_USERDATA));
    if (!self)
      return DefWindowProcA(hWnd, message, wParam, lParam);
  }

  if (self->OnDeviceWatchMessageHandleA(hWnd, message, wParam, lParam)) {
    return 0;
  }

  switch (message) {
    case WM_NCCREATE: {
      CREATESTRUCT* cs = reinterpret_cast<CREATESTRUCT*>(lParam);
      self = reinterpret_cast<UsbWatcherWindows*>(cs->lpCreateParams);
      // Associate |self| with the main window.
      ::SetWindowLongPtrA(hWnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
    } break;

    case WM_CREATE:
      self->hwnd_ = hWnd;
      break;

	  case WM_DESTROY:
      self->hwnd_ = nullptr;
		  PostQuitMessage(0);
		  break;
	}

  return DefWindowProcA(hWnd, message, wParam, lParam);
}

bool UsbWatcherWindows::CreateHiddenWindowA() {
  // register window class
  HINSTANCE hInstance = nullptr;

  static const char szClassName[] = "SWDL_InterfaceNotifyWindow";

  static bool once = false;
  if (!once) {
    once = true;
    WNDCLASSEXA wc = {0};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = WndProcA;
    wc.hInstance = hInstance;
    wc.lpszClassName = szClassName;
    wc.cbWndExtra = sizeof(LPVOID);
    RegisterClassExA(&wc);
  }

  CreateWindowExA(0, szClassName, "hidden_interface_notifier_window", 0,
          0, 0, 0, 0, HWND_MESSAGE, nullptr, hInstance, this);
  return !!hwnd_;
}

void UsbWatcherWindows::createWatch(std::function<void(bool)> &&cb) noexcept {
  initCallback_ = std::move(cb);
  bool result = CreateHiddenWindowA();
  if (!result) {
    if (initCallback_) std::move(initCallback_)(false);
    return;
  }

  MSG msg;
  while (GetMessage(&msg, nullptr, 0, 0)) {
    TranslateMessage(&msg);
    DispatchMessage(&msg);
  }
}

void UsbWatcherWindows::deleteWatch() noexcept {
  if (hwnd_) {
    PostMessage(hwnd_, WM_CLOSE, 0, 0);
  }
}

#ifdef ENABLE_TEST
#include "usb-watch_tests.cc"
#endif

} // namespace device_enumerator

