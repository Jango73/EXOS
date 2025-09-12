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

./scripts/4-4-build.sh

( or ./scripts/4-1-clean-build.sh to later build from a clean repo )

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

## Things it does

* Task management with scheduler
* Virtual memory management
* File system management : FAT, FAT32, EXFS (EXOS file system)
* ATA Hard disk driver
* Console management
* Basic keyboard and mouse management
* Primitive graphics using VESA standard
* Heap management
* Process spawning, task spawning, scheduling
* Shell with minimal functionality
* Virtual file system with mount points
* TOML configuration file

## Things to do

* Kernel pointer masking and ASLR (Address Space Layout Randomization)
* Load ELF executables
* Minimal network drivers
* Add buffers for filesystem drivers
* Improve the scheduler
* Implement some file systems
* Add some drivers/managers (USB, PCIe, NVMe)
* Destroy processes (they live infinitely now)
* Implement Unicode
* Implement I18n
* Add keyboard layouts
* Continue graphics UI
* and all the rest....
