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

./scripts/(arch)/4-4-build-release-ext2.sh

( or ./scripts/(arch)/4-1-clean-build-release-ext2.sh to later build from a clean repo )

Note: (arch) = i386 or x86-64

### Build (Disk image with FAT32)

./scripts/(arch)/4-4-build-release-fat32.sh

( or ./scripts/(arch)/4-1-clean-build-release-fat32.sh to later build from a clean repo )

Note: (arch) = i386 or x86-64

### Run

./scripts/(arch)/5-1-start-qemu-ioapic-sata-e1000.sh

( or ./scripts/(arch)/5-2-debug-qemu-ioapic-sata-e1000.sh to debug with gdb)
( or ./scripts/(arch)/5-5-start-bochs.sh to use Bochs )

Note: (arch) = i386 or x86-64

## Dependencies

### C language (no headers)

### bcrypt
Used for password hashing. Sources in third/bcrypt (under Apache 2.0, see third/bcrypt/README and third/bcrypt/LICENSE).
Compiled files in kernel: bcrypt.c, blowfish.c.
bcrypt is copyright (c) 2002 Johnny Shelley <jshelley@cahaus.com>

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
- ACPI shutdown/reboot
- Console management
- Basic keyboard and mouse management
- Primitive graphics using VESA standard (broken)
- Virtual file system with mount points
- Scripted shell
- Configuration with TOML format
- E1000 network driver
- ARP/IPv4/DHCP/UDP/TCP network layers
- Minimal HTTP client
- Kernel pointer masking
- A few test apps

## Metrics

```
-------------------------------------------------------------------------------
Language                     files          blank        comment           code
-------------------------------------------------------------------------------
C                              130          13683          13915          41583
C/C++ Header                   129           3619           4264           8620
Assembly                        16           1726           1104           4891
-------------------------------------------------------------------------------
SUM:                           275          19028          19283          55094
-------------------------------------------------------------------------------
```

## End of the road?
AI once made it possible for a single person to develop an open-source project without astronomical costs.
Now it has become impossible with the widespread adoption of “usage-based fees,” which makes building AI-powered open-source projects too expensive—especially since they bring in no revenue at all.
So this project will now be coded very slowly if at all.
