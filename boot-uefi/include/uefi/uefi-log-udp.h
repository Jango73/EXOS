
/************************************************************************\

    EXOS UEFI Bootloader
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


    UEFI UDP logger

\************************************************************************/

#ifndef UEFI_LOG_UDP_H_INCLUDED
#define UEFI_LOG_UDP_H_INCLUDED

/************************************************************************/

#include "uefi/efi.h"

/************************************************************************/

void BootUefiUdpLogInitialize(EFI_BOOT_SERVICES* BootServices);
void BootUefiUdpLogNotifyExitBootServices(void);
void BootUefiUdpLogWrite(LPCSTR Text);
U32 BootUefiUdpLogGetInitFlags(void);

#define UEFI_UDP_INIT_FLAG_LOCATE_OK      0x1
#define UEFI_UDP_INIT_FLAG_START_OK       0x2
#define UEFI_UDP_INIT_FLAG_INITIALIZE_OK  0x4
#define UEFI_UDP_INIT_FLAG_ENABLED        0x8

/************************************************************************/

#endif  // UEFI_LOG_UDP_H_INCLUDED
