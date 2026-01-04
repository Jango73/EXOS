# EXOS

## TL;DR

Multi-threaded operating system for i386 and x86_64.
Currently booting in QEMU and Bochs.

## What it is

This is an ongoing OS dev project that was abandoned in late 1999.
Back then, it was 32 bit and compiled with gcc and nasm, but linked with jloc.
Build was recently ported to i686-elf-gcc/nasm/i686-elf-ld.

## Debian compile & run

### Setup dependencies

./scripts/1-1-setup-deps.sh

./scripts/1-2-setup-qemu.sh		<- if you want a recent QEMU (9.0.2)

### Build (Disk image with ext2)

./scripts/build --arch <i386|x86-64> --fs ext2 --release

( add --clean for a clean build )

### Build (Disk image with FAT32)

./scripts/build --arch <i386|x86-64> --fs fat32 --release

( add --clean for a clean build )

### Run

./scripts/run --arch <i386|x86-64>

( add --gdb to debug with gdb )
( or ./scripts/(arch)/5-5-start-bochs.sh to use Bochs )

## Dependencies

### C language (no headers)

### bcrypt
Used for password hashing. Sources in third/bcrypt (under Apache 2.0, see third/bcrypt/README and third/bcrypt/LICENSE).
Compiled files in kernel: bcrypt.c, blowfish.c.
bcrypt is copyright (c) 2002 Johnny Shelley <jshelley@cahaus.com>

### utf8-hoehrmann
Used for UTF-8 decoding in layout parsing. Sources in third/utf8-hoehrmann (MIT license, see headers).

## Historical background

In 1999, I started EXOS as a simple experiment: I wanted to write a minimal OS bootloader for fun.  
Very quickly, I realized I was building much more than a bootloader. I began to re-implement full system headers, taking inspiration from Windows and low-level DOS/BIOS references, aiming to create a complete 32-bit OS from scratch.
It was a year-long solo project, developed the hard way:
- On a Pentium, in DOS environment, without any debugger or VM
- Relying on endless console print statements to trace bugs
- Learning everything on the fly as the project grew

## Things it does

- Multi-architecture : i386, x86-64
- Virtual memory management (paging)
- Heap management (free lists)
- Process spawning, task spawning, scheduling
- File system management : FAT16, FAT32, EXT2, EXFS (EXOS file system)
- I/O APIC management
- PCI device management
- ATA & SATA/AHCI hard disk drivers
- xHCI driver (USB 3)
- ACPI shutdown/reboot
- Console management
- PS/2 keyboard and mouse drivers
- USB keyboard (HID) and mouse drivers
- USB mass storage device driver
- Primitive graphics using VESA standard (broken)
- Virtual file system with mount points
- Scripted shell with kernel object exposure
- Configuration with TOML format
- E1000 network driver
- ARP/IPv4/DHCP/UDP/TCP network layers
- Minimal HTTP client
- Kernel pointer masking, handles in userland
- A few test apps

## Metrics (cloc)

```
-------------------------------------------------------------------------------
Language                     files          blank        comment           code
-------------------------------------------------------------------------------
C                              185          20136          20614          66312
C/C++ Header                   165           4434           5192          10746
Assembly                        16           1752           1128           5010
-------------------------------------------------------------------------------
SUM:                           366          26322          26934          82068
-------------------------------------------------------------------------------
```
