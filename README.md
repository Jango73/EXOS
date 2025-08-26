# EXOS

## TL;DR

Old operating system for i386.
Currently booting in qemu.

## What it is

This was an attempt, around 1999, to create an operating system.
It is work in progress.

Because it is a very long work, it will probably never be finished.

## Debian compile & run

### Setup dependencies

./scripts/1-setup-deps.sh

### Build

./scripts/4-2-build.sh

( or ./scripts/4-1-clean-build-exos.sh to later build from a clean repo )

(Output is in log/make.log)

### Run

./scripts/5-1-start-qemu-hd.sh

( or 5-2-debug-qemu-hd.sh to debug )  
( or 6-1-start-qemu-hd-nogfx.sh on a pure TTY Debian)

## Historical background

In 1999, I started EXOS as a simple experiment: I wanted to write a minimal OS bootloader for fun.  
Very quickly, I realized I was building much more than a bootloader. I began to re-implement full system headers, taking inspiration from Windows and low-level DOS/BIOS references, aiming to create a complete 32-bit OS from scratch.

This was a year-long solo project, developed the hard way:
- On a Pentium, without any debugger or VM
- Relying on endless print statements to trace bugs
- Learning everything on the fly as the project grew

I stopped development when I hit a wall: the scheduler was too slow, I lost motivation at that time.

## Timeline & resilience

In 2025, after more than 25 years in storage, I managed to revive EXOS.
The original routines (disk access, FAT32 handling, task/process logic) booted and worked out of the box on modern QEMU virtual machines, with image files replacing the physical hard drives of the 90s.

- Hand-written drivers for IDE/ATA and interrupt management: still working
- FAT32 filesystem code from 1999: able to mount and browse modern images
- Survived several generations of toolchains (GCC, NASM, mkfs, QEMU)

> In 1999, EXOS read its first FAT32 disk on real hardware.
> In 2025, it does exactly the same under QEMU.

## Things it does

* Task management with scheduler
* Virtual memory management
* File system management : FAT, FAT32, XFS (EXOS file system)
* SATA Hard disk driver
* Console management
* Basic keyboard and mouse management
* Primitive graphics using VESA standard
* Heap management
* Process spawning, task spawning, scheduling

## Things to do

* Add buffers for filesystem drivers
* Rework the scheduler and task switching...
* Implement a POSIX-like virtual file system
* Implement some file systems
* Add some drivers
* Destroy processes (they live infinitely now)
* Continue graphics UI
* Load ELF executables
* and all the rest....
