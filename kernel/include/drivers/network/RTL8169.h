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


    RTL8169

\************************************************************************/

#ifndef RTL8169_H_INCLUDED
#define RTL8169_H_INCLUDED

/***************************************************************************/

#include "Base.h"
#include "Driver.h"
#include "drivers/bus/PCI.h"
#include "network/Network.h"

/***************************************************************************/

#define RTL8169_VENDOR_REALTEK 0x10EC
#define RTL8169_DEVICE_8161 0x8161
#define RTL8169_DEVICE_8168 0x8168

/***************************************************************************/

#define RTL8169_MATCH_ENTRY(DeviceID) \
    { RTL8169_VENDOR_REALTEK, DeviceID, PCI_CLASS_NETWORK, PCI_SUBCLASS_ETHERNET, PCI_ANY_CLASS }

/***************************************************************************/

extern PCI_DRIVER RTL8169Driver;
LPDRIVER RTL8169GetDriver(void);

/***************************************************************************/

#endif  // RTL8169_H_INCLUDED
