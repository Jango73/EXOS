# EXOS

## TL;DR

Operating system for i386.
Currently booting in QEMU and Bochs.

## What it is

This is an ongoing OS dev project that was abandoned in late 1999.
Back then, it compiled with gcc and nasm, but linked with jloc.
Build was recently ported to i686-elf-gcc/nasm/i686-elf-ld.

## Debian compile & run

### Setup dependencies

./scripts/1-1-setup-deps.sh

./scripts/1-2-setup-qemu.sh		<- if you want a recent QEMU (9.0.2)

### Build

./scripts/4-4-build.sh

( or ./scripts/4-1-clean-build.sh to later build from a clean repo )

### Run

./scripts/5-1-start-qemu-ioapic-sata-e1000.sh

( or 5-2-debug-qemu-ioapic-sata-e1000.sh to debug )
( or 5-5-start-bochs.sh to use Bochs )

## Dependencies

### C language (no headers)

### bcrypt
Used for password hashing. Sources in third/bcrypt (under Apache 2.0, see third/bcrypt/README and third/bcrypt/LICENSE).
Compiled files in kernel: bcrypt.c, blowfish.c.
bcrypt is copyright (c) 2002 Johnny Shelley <jshelley@cahaus.com>

## Historical background

In 1999, I started EXOS as a simple experiment: I wanted to write a minimal OS bootloader for fun.  
Very quickly, I realized I was building much more than a bootloader. I began to re-implement full system headers, taking inspiration from Windows and low-level DOS/BIOS references, aiming to create a complete 32-bit OS from scratch.

This was a year-long solo project, developed the hard way:
- On a Pentium, in DOS environment, without any debugger or VM
- Relying on endless console print statements to trace bugs
- Learning everything on the fly as the project grew

## Things it does

- Virtual memory management
- Heap management
- Process spawning, task spawning, scheduling
- File system management : FAT16, FAT32, EXFS (EXOS file system)
- PCI device management
- ATA Hard disk driver
- SATA/AHCI Hard disk driver
- I/O APIC management
- ACPI Shutdown/reboot
- Console management
- Basic keyboard and mouse management
- Primitive graphics using VESA standard
- Virtual file system with mount points
- Scripted shell
- Configuration with TOML format
- E1000 network driver
- ARP/IPv4/DHCP/UDP/TCP network layers
- Minimal HTTP client
