/************************************************************************\

    EXOS Kernel
    Copyright (c) 1999-2026 Jango73

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


    RTL8139CPlus

\************************************************************************/

#ifndef RTL8139CPLUS_H_INCLUDED
#define RTL8139CPLUS_H_INCLUDED

/***************************************************************************/

#include "drivers/network/RealtekCommon.h"

/***************************************************************************/

#define RTL8139CPLUS_VENDOR_REALTEK REALTEK_NETWORK_VENDOR_ID
#define RTL8139CPLUS_DEVICE_8139 0x8139
#define RTL8139CPLUS_MINIMUM_REVISION 0x20
#define RTL8139CPLUS_MAXIMUM_MTU 1500

/***************************************************************************/

extern PCI_DRIVER RTL8139CPlusDriver;
LPDRIVER RTL8139CPlusGetDriver(void);

/***************************************************************************/

#endif  // RTL8139CPLUS_H_INCLUDED
