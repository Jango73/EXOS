# EXOS

## What it is

This was an attempt, around 1997, to create an operating system.
It is work in progress.

Because it is a very long work, it will probably never be finished.

## Historical background

In 1999, I started EXOS as a simple experiment: I wanted to write a minimal OS bootloader for fun.  
Very quickly, I realized I was building much more than a bootloader. I began to re-implement full system headers, taking inspiration from Windows and low-level DOS/BIOS references, aiming to create a complete 32-bit OS from scratch.

This was a year-long solo project, developed the hard way:  
- On a Pentium, without any debugger or VM  
- Relying on endless print statements to trace the bugs  
- Learning everything on the fly as the project grew

I stopped development when I hit a wall: the scheduler was too slow and complex for my hardware and tools at the time. Task switching took forever.

## Timeline & resilience

In 2025, after more than 25 years in storage, I managed to revive EXOS with almost no changes.  
The original routines (disk access, FAT32 handling, task/process logic) booted and worked out of the box on modern QEMU virtual machines, with image files replacing the physical hard drives of the 90s.

- BIOS boot sector (512 bytes), custom loader, protected mode transition: still working  
- Hand-written drivers for IDE/ATA and interrupt management: still working  
- FAT32 filesystem code from 1999: able to mount and browse modern images  
- Survived several generations of toolchains (GCC, NASM, mkfs, QEMU) without major rewrite

> In 1999, EXOS read its first FAT32 disk on real hardware.  
> In 2025, it does exactly the same under QEMU.  
> Same codebase, different century.

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

* Create makefiles for build (used to build on DOS using gcc and nasm)
* Rework the scheduler and task switching...
* Add some file systems
* Add some drivers
* Destroy processes (they live infinitely now)
* Continue graphics UI
* Load ELF executables
* and all the rest....
