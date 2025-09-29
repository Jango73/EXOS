# EXOS Project Structure

## Overview

EXOS is a 32-bit operating system for the i386 architecture, developed since 1999.  
This document describes the organization of the git repository directories.

## Directory Tree

### `/boot-floppy/`
Floppy bootloader with assembly files (boot-floppy.asm, hd-sector0.asm).

### `/boot-freedos/`
Configuration and files for booting via FreeDOS, including system TOML configurations.

### `/boot-hd/`
Scripts and tools for creating bootable disk images. Contains subdirectories for kernel, runtime, system, and tools, as well as linker scripts.

### `/build/`
Build directory containing generated binaries and temporary build files.

### `/deploy/`
System deployment scripts and tools. Not used yet.

### `/documentation/`
Project documentation organized into two subdirectories:
- `external/`: User documentation
- `internal/`: Internal technical documentation, specifications, and plans

### `/kernel/`
Operating system kernel:
- `include/`: Kernel C headers
- `source/`: Kernel C and assembly source code
- Build and configuration scripts

### `/log/`
Log files generated during execution (kernel.log, qemu.log, etc.).

### `/runtime/`
Runtime library for user applications:
- `include/`: Runtime API headers
- `source/`: Runtime implementation (exos.c, http.c, etc.)

### `/scripts/`
Project automation scripts:
- Dependency and QEMU setup
- Build and code formatting scripts
- Documentation generation
- Test and debug scripts

### `/system/`
System applications and examples:
- `hello/`: Hello World application
- `netget/`: Network utility
- `portal/`: Portal application
- `test/`: Test applications
- `tictactoe/`: Example game

### `/temp/`
Temporary directory for working files.

### `/third/`
Third-party libraries:
- `bcrypt/`: Bcrypt implementation for encryption
- `include/`: Placeholders for standard libs

### `/tools/`
Development tools and system utilities with their sources.

### `/xfs-manager/`
EXFS filesystem manager with C++ and assembly code.

## Root Files

- `CLAUDE.md`: Instructions for AI agents
- `Makefile`: Main project build
- `README.md`: Main documentation
- `LICENSE`: GPL v3 license
- `.clang-format`: Code formatting configuration
- `Doxyfile`: Doxygen configuration for documentation
- `dashboard.*`: System monitoring interface

## Conventions

- C/assembly code for the kernel in `/kernel/`
- User-space applications in `/system/`
- Technical documentation in `/documentation/internal/`
- Automation scripts in `/scripts/`
- Isolated third-party libraries in `/third/`
- Build artifacts in `/build/` (not versioned)
- Execution logs in `/log/` (not versioned)
