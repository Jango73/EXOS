# CLAUDE.md

This file provides guidance to agents when working with code in this repository.

**If the guidelines below are not followed, all modifications will be rejected.**

## Communication Guidelines
- NEVER use emojis in responses
- DON'T act like a human being with emotions, just be a machine.
- DON'T says "Great!", "Perfect!", "You're right" all the time.
- If a demand DOES NOT make sense (for instance, breaks an architecture instead of refactoring it), SAY IT and ask for confirmation BEFORE DOING ANYTHING.

## Project Overview
EXOS is a 32-bit operating system for i386 architecture, originally developed in 1999. It features multitasking, virtual memory management, file systems (FAT/FAT32/EXFS), hardware drivers, and a basic shell interface.

## Coding Conventions
- **Debugging**: Debug output is **ALWAYS** logged with DEBUG(). Warnings are logged with WARNING() and errors with ERROR(), verbose is done with VERBOSE()
- **Logging**: A log string **ALWAYS** begins with "[FunctionName]" where FunctionName is the name of function where the logging is done.
- **I18n**: Write comments, console output and technical doc in english
- **Naming**: PascalCase for variables/members, SCREAMING_SNAKE_CASE for structs/defines
- **Comments**: For single-line comments, use `//`, not `/*`
- **Style**: 4-space indentation, follow `.clang-format` rules
- **Numbers**: Hexadecimal for constant numbers, except for graphic points/sizes and time
- **Documentation**: Update `documentation/internal/Kernel.md` when adding/modifying kernel components
- **Languages**: C for kernel, avoid Python (use Node.js/JS if needed)
- **Libraries**: NO stdlib/stdio in kernel - custom implementations only
- **Unused parameters**: Use the macro UNUSED() to suppress the "unused parameter" warning
- **Pointers**: In the kernel, before using a kernel object pointer, use the appropriate macro for this : SAFE_USE (or SAFE_USE_VALID but it is slower) if you got a pointer to any kind of object, SAFE_USE_VALID_ID if you got a pointer to a kernel object **which inherits LISTNODE_FIELDS**. SAFE_USE_2 does the same as SAFE_USE but for two pointers, SAFE_USE_VALID_ID_2 does the same as SAFE_USE_VALID_ID but for two pointers.
- **No direct access to physical memory**: Use the MapTempPhysicalPage and MapIOMemory/UnMapIOMemory functions to access physical memory pages
- **Clean code**: No duplicate code. Create intermediate functions to avoid it.
- **Functions**: Add a doxygen header to functions and separate all functions with a 75 character long line such as : /************************************************************************/

## Common Build Commands

**Use `./scripts/4-2-clean-build-debug.sh` for complete build and `./scripts/4-5-build-debug.sh` for incremental build when unsure which build script to use.**

**Incremental debug build:**
```bash
./scripts/4-5-build-debug.sh             # Debug build
./scripts/4-6-build-scheduling-debug.sh  # Scheduling debug logs : DON'T USE
```

**Clean build:**
```bash
./scripts/4-2-clean-build-debug.sh    # Clean then build debug
./scripts/4-3-clean-build-scheduling-debug.sh # Clean then build with debug and scheduling debug logs : DON'T USE
```

**Run in QEMU:**
```bash
./scripts/5-1-start-qemu-ioapic-sata-e1000.sh  # Launches QEMU
./scripts/5-2-debug-qemu-ioapic-sata-e1000.sh  # Launches QEMU with graphics and GDB
```

**Don't wait more than 10 seconds when testing, the system boots in less than 2 seconds and auto-run executable should finish under 8 seconds**

## Debug output

Kernel debug output goes to `log/kernel.log`.
QEMU traces go to `qemu.log`.
Bochs output goes to `bochs.log`.
**Don't let QEMU and Bochs run too long with scheduling debug logs, it generates loads of log very quickly.**

## Documentation

Kernel design : `documentation/internal/Kernel.md`
Doxygen documentation is in `documentation/internal/kernel/*`

**Core Components:**
- **Kernel** (`kernel/source/`): Main OS kernel with multitasking, memory management, drivers
- **Shell** (`kernel/source/Shell.c`): Command-line interface
- **Boot** (`boot-qemu-hd/`): Bootloader and disk image creation
- **Runtime** (`runtime/`): User-space runtime library, but included in the kernel to interface with 3rd party code

## Architecture Overview

**Key Modules:**
- `Kernel.c`: Initialization sequence and global objects
- `Mutex.c`: Synchronization primitives
- `Memory.c`: Memory management
- `Heap.c`: Heap management
- `Interrupt.c`: IDT creation
- `InterruptController.c`: IOAPIC management
- `Interrupt-a.asm`: Fault and ISR stubs
- `Clock.c`: RTC and scheduler caller
- `Fault.c`: Fault handlers like #GP, #PF, #UD, ...
- `Process.c`: Process management
- `Task.c`: Task management
- `Schedule.c`: Task scheduler
- `Console.c`: Console I/O
- `FileSystem.c`, `FAT16.c`, `FAT32.c`, `EXFS.c`: File system implementations
- `SystemFS.c`: POSIX-like virtual file system
- `System.asm`: Many small bare-metal routines (LGDT, GetCR4, etc...)

## Debug Workflow
1. Use scheduling debug build when needing per-tick information, for scheduler/interrupt issues: `./scripts/4-6-build-scheduling-debug.sh` (or `./scripts/4-3-clean-build-scheduling-debug.sh` for a clean make) : GENERATES TONS OF LOG, USE WITH CARE.
2. Monitor `log/kernel.log` for exceptions and page faults
3. **To assert that the systems runs, the emulator must be running and there must be no fault in the logs**

**Disassembly Analysis:**
- `./scripts/utils/show.sh <address> [context_lines]` - Shows disassembly around any address in exos.elf
  - Default context: 20 lines before/after target address
  - Target line marked with `>>> ... <<<`
  - Example: `./scripts/utils/show.sh 0xc0123456 5` (5 lines context)
