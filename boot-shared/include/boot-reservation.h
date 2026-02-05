/************************************************************************\

    EXOS Bootloader
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


    Boot reservation policy constants

\************************************************************************/

#ifndef BOOT_RESERVATION_H_INCLUDED
#define BOOT_RESERVATION_H_INCLUDED

#include "Base.h"

/************************************************************************/
// Kernel image reservation policy

#define BOOT_KERNEL_MAP_PADDING_BYTES ((UINT)N_512KB)
#define BOOT_KERNEL_TABLE_WORKSPACE_BYTES ((UINT)N_1MB)
#define BOOT_KERNEL_IDENTITY_WORKSPACE_BYTES ((UINT)N_2MB)

/************************************************************************/
// x86-64 transition workspace helpers

#define BOOT_X86_64_TEMP_LINEAR_REQUIRED_SPAN ((U32)0x00103000)
#define BOOT_X86_64_PAGE_TABLE_SIZE ((U32)N_4KB)
#define BOOT_X86_64_PAGE_TABLE_ENTRIES ((U32)512)

#endif  // BOOT_RESERVATION_H_INCLUDED
