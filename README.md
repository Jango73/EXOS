# EXOS

## TL;DR

Multi-threaded operating system for x86-32 and x86-64.
Tested on QEMU, Bochs, ACER Predator.

## What it is

This is an ongoing operating system project that was abandoned in late 1999.
Back then, it was 32 bit only and compiled with gcc and nasm, and linked with jloc.
Build was recently ported to i686-elf-gcc/nasm/i686-elf-ld, then ported to x86-64.

## Disclaimer

EXOS is provided "as is", without warranty of any kind. Neither EXOS authors/contributors, nor the authors/contributors of bundled third-party software, can be held liable for any direct, indirect, incidental, special, exemplary, or consequential damages arising from the use of this project.

## Debian compile & run

### Setup dependencies

./scripts/1-1-setup-deps.sh

./scripts/1-2-setup-qemu.sh		<- if you want a recent QEMU (9.0.2)

### Build (Disk image with ext2)

./scripts/build --arch <x86-32|x86-64> --fs ext2 --release (or --debug)

( add --clean for a clean build )

### Build (Disk image with FAT32)

./scripts/build --arch <x86-32|x86-64> --fs fat32 --release (or --debug)

( add --clean for a clean build )

### Build for UEFI boot

./scripts/build --arch <x86-32|x86-64> --fs ext2 --release (or --debug) --uefi

( add --clean for a clean build )

### Run

./scripts/run --arch <x86-32|x86-64>

( add --gdb to debug with gdb )
( or ./scripts/(arch)/5-5-start-bochs.sh to use Bochs )

## Dependencies

### C language (no headers)

### bcrypt
Used for password hashing. Sources in third/bcrypt (under Apache 2.0, see third/bcrypt/README and third/bcrypt/LICENSE).
Compiled files in kernel: bcrypt.c, blowfish.c.
bcrypt is copyright (c) 2002 Johnny Shelley <jshelley@cahaus.com>

### BearSSL
Used for SHA-256 hashing in kernel crypt utilities. Sources in `third/bearssl` (MIT license, see `third/bearssl/LICENSE.txt` and `third/bearssl/README.txt`).
Integrated SHA-256 sources: `third/bearssl/src/hash/sha2small.c`, `third/bearssl/src/codec/dec32be.c`, `third/bearssl/src/codec/enc32be.c`.
BearSSL is copyright (c) 2016 Thomas Pornin <pornin@bolet.org>.

### miniz
Used for DEFLATE/zlib compression in kernel compression utilities. Sources in `third/miniz` (MIT license, see `third/miniz/LICENSE` and `third/miniz/readme.md`).
Integrated kernel backend source: `third/miniz/miniz.c`.
miniz is copyright (c) Rich Geldreich, RAD Game Tools, and Valve Software.

### Monocypher
Used for detached signature verification (Ed25519) in kernel signature utilities. Sources in `third/monocypher` (BSD-2-Clause OR CC0-1.0, see `third/monocypher/LICENCE.md` and `third/monocypher/README.md`).
Integrated signature backend sources: `third/monocypher/src/monocypher.c` and `third/monocypher/src/optional/monocypher-ed25519.c`.
For kernel freestanding compatibility, Monocypher Argon2 is disabled in x86-32 builds.
Monocypher is copyright (c) 2017-2019 Loup Vaillant.

### utf8-hoehrmann
Used for UTF-8 decoding in layout parsing. Sources in third/utf8-hoehrmann (MIT license, see headers).

### Fonts
Bm437_IBM_VGA_8x16.otb from the Ultimate Oldschool PC Font Pack by VileR, licensed under CC BY-SA 4.0. See third/fonts/oldschool_pc_font_pack/ATTRIBUTION.txt and third/fonts/oldschool_pc_font_pack/LICENSE.TXT.

## Things it does

- Multi-architecture : x86-32, x86-64
- Virtual memory management (paging) with a buddy allocator for physical pages
- Heap management (free lists)
- Process spawning, task spawning, scheduling
- Security at kernel object level
- File system management : FAT16, FAT32, EXT2, NTFS, EXFS (EXOS file system)
- I/O APIC & Local APIC management
- PCI device management
- ATA, SATA/AHCI & NVMe storage drivers
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

## Historical background

In 1999, I started EXOS as a simple experiment: I wanted to write a minimal OS bootloader for fun.  
Very quickly, I realized I was building much more than a bootloader. I began to re-implement full system headers, taking inspiration from Windows and low-level DOS/BIOS references, aiming to create a complete 32-bit OS from scratch.
It was a year-long solo project, developed the hard way:
- On a Pentium, in DOS environment, without any debugger or VM
- Relying on endless console print statements to trace bugs
- Learning everything on the fly as the project grew

## Metrics (cloc)

Lines of code this project, excluding third party software.

```
-------------------------------------------------------------------------------
Language                     files          blank        comment           code
-------------------------------------------------------------------------------
C                              253          25123          26698          85478
C/C++ Header                   182           4993           5406          12607
Assembly                        20           1861           1214           5750
-------------------------------------------------------------------------------
SUM:                           455          31977          33318         103835
-------------------------------------------------------------------------------
```
