
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


    FSID

\************************************************************************/
#ifndef FSID_H_INCLUDED
#define FSID_H_INCLUDED

/***************************************************************************/

// This is a list of known partition types

#define FSID_NONE 0x00
#define FSID_DOS_FAT12 0x01        // DOS 12-bit FAT
#define FSID_XENIXROOT 0x02        // XENIX root
#define FSID_XENIXUSER 0x03        // XENIX usr
#define FSID_DOS_FAT16S 0x04       // DOS 16-bit FAT smaller than 32 MB
#define FSID_EXTENDED 0x05         // Extended Partition
#define FSID_DOS_FAT16L 0x06       // DOS 16-bit FAT larger than 32 MB
#define FSID_OS2_HPFS 0x07         // OS2 HPFS, NTFS
#define FSID_DOS_AIX 0x08          // DOS, AIX
#define FSID_DOS_AIX_BOOT 0x09     // DOS, AIX bootable
#define FSID_OS2_BOOTMAN 0x0A      // OS2 Boot Manager
#define FSID_DOS_FAT32 0x0B        // DOS 32-bit FAT
#define FSID_DOS_FAT32_LBA1 0x0C   // DOS 32-bit FAT using LBA1 Extensions
#define FSID_DOS_FAT16L_LBA1 0x0E  // DOS 16-bit FAT using LBA1 Extensions
#define FSID_DOS_FAT32X 0x0F       // DOS 32-bit FAT
#define FSID_OPUS 0x10             // OPUS
#define FSID_HIDDEN_DOS_FAT12 0x11
#define FSID_HIDDEN_IFS 0x17
#define FSID_NEC_DOS_3X 0x24
#define FSID_NOS 0x32
#define FSID_THEOS 0x38
#define FSID_VENIX 0x40        // Venix 80286
#define FSID_POWERPC 0x41      // PowerPC
#define FSID_SFS 0x42          // Secure Filesystem
#define FSID_GOBACK 0x44       // GoBack partition
#define FSID_BOOT_US 0x45      // Boot-US boot manager
#define FSID_ADAOS 0x4A        // AdaOS Aquila
#define FSID_OBERON 0x4C       // Oberon partition
#define FSID_NOVELL 0x51       // Novell
#define FSID_MICROPORT 0x52    // Microport
#define FSID_EZDRIVE 0x55      // EZ-Drive
#define FSID_UNIX_V 0x63       // Unix System V
#define FSID_NOVELL_NET1 0x64  // Novell Netware
#define FSID_NOVELL_NET2 0x65  // Novell Netware
#define FSID_PC_IX 0x75        // PC/IX
#define FSID_XOSL 0x78         // XOSL FS
#define FSID_OLD_MINIX 0x80    // Old Minix
#define FSID_LINUXMINIX 0x81   // Linux / Minix
#define FSID_LINUXSWAP 0x82    // Linux Swap
#define FSID_LINUXNATIVE 0x83   // Linux Native
#define FSID_LINUX_EXT2 0x83    // Linux EXT2
#define FSID_LINUX_EXT3 0x83    // Linux EXT3
#define FSID_LINUX_EXT4 0x83    // Linux EXT4
#define FSID_LINUX_EXTENDED 0x85  // Linux Extended
#define FSID_LINUX_LVM 0x8E     // Linux LVM
#define FSID_AMOEBA 0x93       // Amoeba
#define FSID_MACOS_X 0xA8      // Mac-OS X
#define FSID_NETBSD 0xA9       // NetBSD
#define FSID_BSD386 0xA5       // BSD 386
#define FSID_BEOS 0xEB         // BeOS
#define FSID_DOS_SECOND 0xF2   // DOS Secondary
#define FSID_EXOS 0xF8         // EXOS
#define FSID_XENIX_BBT 0xFF    // Xenix Bad Block Table

/***************************************************************************/

#endif
