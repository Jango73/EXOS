
/************************************************************************\

    EXOS Kernel
    Copyright (c) 1999-2025 Jango73

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <https://www.gnu.org/licenses/>.


    USB Core Types

\************************************************************************/

#ifndef USB_H_INCLUDED
#define USB_H_INCLUDED

/***************************************************************************/

#include "Base.h"
#include "Device.h"

/***************************************************************************/

#pragma pack(push, 1)

/***************************************************************************/
// Defines

#define USB_SPEED_LS 1
#define USB_SPEED_FS 2
#define USB_SPEED_HS 3
#define USB_SPEED_SS 4

#define USB_ENDPOINT_TYPE_CONTROL 0
#define USB_ENDPOINT_TYPE_ISOCHRONOUS 1
#define USB_ENDPOINT_TYPE_BULK 2
#define USB_ENDPOINT_TYPE_INTERRUPT 3

#define USB_ADDRESS_MIN 0
#define USB_ADDRESS_MAX 127

#define USB_DESCRIPTOR_TYPE_DEVICE 0x01
#define USB_DESCRIPTOR_TYPE_CONFIGURATION 0x02
#define USB_DESCRIPTOR_TYPE_STRING 0x03
#define USB_DESCRIPTOR_TYPE_INTERFACE 0x04
#define USB_DESCRIPTOR_TYPE_ENDPOINT 0x05
#define USB_DESCRIPTOR_TYPE_HUB 0x29
#define USB_DESCRIPTOR_TYPE_SUPERSPEED_HUB 0x2A

#define USB_DESCRIPTOR_LENGTH_DEVICE 18
#define USB_DESCRIPTOR_LENGTH_CONFIGURATION 9
#define USB_DESCRIPTOR_LENGTH_INTERFACE 9
#define USB_DESCRIPTOR_LENGTH_ENDPOINT 7

#define USB_REQUEST_DIRECTION_IN 0x80
#define USB_REQUEST_DIRECTION_OUT 0x00
#define USB_REQUEST_TYPE_STANDARD 0x00
#define USB_REQUEST_TYPE_CLASS 0x20
#define USB_REQUEST_RECIPIENT_DEVICE 0x00
#define USB_REQUEST_RECIPIENT_INTERFACE 0x01
#define USB_REQUEST_RECIPIENT_ENDPOINT 0x02
#define USB_REQUEST_RECIPIENT_OTHER 0x03

#define USB_REQUEST_GET_STATUS 0x00
#define USB_REQUEST_CLEAR_FEATURE 0x01
#define USB_REQUEST_SET_FEATURE 0x03
#define USB_REQUEST_SET_ADDRESS 0x05
#define USB_REQUEST_GET_DESCRIPTOR 0x06
#define USB_REQUEST_SET_CONFIGURATION 0x09

#define USB_FEATURE_ENDPOINT_HALT 0x00

#define USB_CLASS_MASS_STORAGE 0x08
#define USB_CLASS_HUB 0x09

#define USB_HUB_FEATURE_PORT_CONNECTION 0x00
#define USB_HUB_FEATURE_PORT_ENABLE 0x01
#define USB_HUB_FEATURE_PORT_SUSPEND 0x02
#define USB_HUB_FEATURE_PORT_OVER_CURRENT 0x03
#define USB_HUB_FEATURE_PORT_RESET 0x04
#define USB_HUB_FEATURE_PORT_POWER 0x08
#define USB_HUB_FEATURE_PORT_LOW_SPEED 0x09
#define USB_HUB_FEATURE_PORT_HIGH_SPEED 0x0A
#define USB_HUB_FEATURE_C_PORT_CONNECTION 0x10
#define USB_HUB_FEATURE_C_PORT_ENABLE 0x11
#define USB_HUB_FEATURE_C_PORT_SUSPEND 0x12
#define USB_HUB_FEATURE_C_PORT_OVER_CURRENT 0x13
#define USB_HUB_FEATURE_C_PORT_RESET 0x14

/***************************************************************************/
// Typedefs

typedef U8 USB_SPEED;
typedef U8 USB_ENDPOINT_TYPE;
typedef U8 USB_ADDRESS;

typedef struct tag_USB_DEVICE_DESCRIPTOR {
    U8 Length;
    U8 DescriptorType;
    U16 UsbVersion;
    U8 DeviceClass;
    U8 DeviceSubClass;
    U8 DeviceProtocol;
    U8 MaxPacketSize0;
    U16 VendorID;
    U16 ProductID;
    U16 DeviceVersion;
    U8 ManufacturerIndex;
    U8 ProductIndex;
    U8 SerialNumberIndex;
    U8 NumConfigurations;
} USB_DEVICE_DESCRIPTOR, *LPUSB_DEVICE_DESCRIPTOR;

typedef struct tag_USB_CONFIGURATION_DESCRIPTOR {
    U8 Length;
    U8 DescriptorType;
    U16 TotalLength;
    U8 NumInterfaces;
    U8 ConfigurationValue;
    U8 ConfigurationIndex;
    U8 Attributes;
    U8 MaxPower;
} USB_CONFIGURATION_DESCRIPTOR, *LPUSB_CONFIGURATION_DESCRIPTOR;

typedef struct tag_USB_INTERFACE_DESCRIPTOR {
    U8 Length;
    U8 DescriptorType;
    U8 InterfaceNumber;
    U8 AlternateSetting;
    U8 NumEndpoints;
    U8 InterfaceClass;
    U8 InterfaceSubClass;
    U8 InterfaceProtocol;
    U8 InterfaceIndex;
} USB_INTERFACE_DESCRIPTOR, *LPUSB_INTERFACE_DESCRIPTOR;

typedef struct tag_USB_ENDPOINT_DESCRIPTOR {
    U8 Length;
    U8 DescriptorType;
    U8 EndpointAddress;
    U8 Attributes;
    U16 MaxPacketSize;
    U8 Interval;
} USB_ENDPOINT_DESCRIPTOR, *LPUSB_ENDPOINT_DESCRIPTOR;

typedef struct tag_USB_STRING_DESCRIPTOR {
    U8 Length;
    U8 DescriptorType;
    U16 String[1];
} USB_STRING_DESCRIPTOR, *LPUSB_STRING_DESCRIPTOR;

typedef struct tag_USB_PORT_STATUS {
    U16 Status;
    U16 Change;
} USB_PORT_STATUS, *LPUSB_PORT_STATUS;

typedef struct tag_USB_SETUP_PACKET {
    U8 RequestType;
    U8 Request;
    U16 Value;
    U16 Index;
    U16 Length;
} USB_SETUP_PACKET, *LPUSB_SETUP_PACKET;

/***************************************************************************/
// USB device base fields

#define USB_DEVICE_FIELDS           \
    DEVICE_FIELDS                   \
    USB_ADDRESS Address;            \
    U8 SpeedId;                     \
    U16 MaxPacketSize0;             \
    USB_DEVICE_DESCRIPTOR DeviceDescriptor; \
    U8 SelectedConfigValue;         \
    U8 StringManufacturer;          \
    U8 StringProduct;               \
    U8 StringSerial;

/***************************************************************************/

#pragma pack(pop)

/***************************************************************************/

#endif  // USB_H_INCLUDED
