# AGENTS.md

This file provides guidance to agents when working with code in this repository.

## Project Overview
This is a multi-architecture operating system. Currently supporting i386 and x86-64.

**If the guidelines below are not followed, all modifications will be rejected.**

## Communication Guidelines
- NEVER use emojis in responses.
- DON'T act like a human being with emotions, just be a machine.
- DON'T says "Great!", "Perfect!", "You're right" all the time.
- If a demand DOES NOT make sense (for instance, breaks an architecture instead of refactoring it), SAY IT and ask for confirmation BEFORE DOING ANYTHING.

## Coding Conventions
- **Types**: Use **LINEAR** for virtual addresses (when not using direct pointers), **PHYSICAL** for physical addresses, **UINT** for indexes, sizes and error values. In the kernel, it is **STRICTLY FORBIDDEN** to use a direct c type (int, unsigned long, long long, etc...) : **only types in Base.h are allowed.**
- **Freestanding**: The codebase **MUST NOT** rely on **ANY** library/module outside of the EXOS codebase. **NO** stdlib, stdio, whatever. Everything the kernel needs is built in the compiler and in the codebase.
- **Debugging**: Debug output is logged with DEBUG(). Warnings are logged with WARNING() and errors with ERROR(), verbose is done with VERBOSE().
- **Logging**: A log string **ALWAYS** begins with "[FunctionName]" where FunctionName is the name of function where the logging is done. Use "%p" for pointers and adresses, "%x" for values except for sizes which use "%u".
- **I18n**: Write comments, console output and technical doc in english.
- **Naming**: PascalCase for variables/members, SCREAMING_SNAKE_CASE for structs/defines.
- **Order**: Group the declarations in headers. 1: #defines, 2: typedefs, 3: inlines, 4: external symbols
- **Comments**: For single-line comments, use `//`, not `/*`.
- **Style**: 4-space indentation, follow `.clang-format` rules.
- **Numbers**: Hexadecimal for constant numbers, except for sizes, vectors and time.
- **Documentation**: Update `documentation/internal/Kernel.md` when adding/modifying kernel components.
- **Languages**: C for kernel, avoid Python (use Node.js/JS if needed).
- **Libraries**: NO stdlib/stdio in kernel - custom implementations only.
- **Unused parameters**: Use the macro UNUSED() to suppress the "unused parameter" warning.
- **Pointers**: In the kernel, before using a kernel object pointer, use the appropriate macro for this : SAFE_USE if you got a pointer to any kind of object, SAFE_USE_VALID_ID if you got a pointer to a kernel object **which inherits LISTNODE_FIELDS**. SAFE_USE_2 does the same as SAFE_USE but for two pointers, SAFE_USE_VALID_ID_2 does the same as SAFE_USE_VALID_ID but for two pointers (SAFE_USE_VALID_ID_3 for 3 pointers, etc...).
- **No direct access to physical memory**: Use the MapTemporaryPhysicalPage1 (MapTemporaryPhysicalPage2, etc...) and MapIOMemory/UnMapIOMemory functions to access physical memory pages.
- **Clean code**: No duplicate code. Create intermediate functions to avoid it.
- **Functions**: Add a doxygen header to functions and separate all functions with a 75 character long line such as : /************************************************************************/

## Common Build Commands

All helper scripts are organized per architecture:
- i386 scripts live in `./scripts/i386/`
- x86-64 scripts live in `./scripts/x86-64/`

**i386 debug build workflow**

**Use `./scripts/i386/4-2-clean-build-debug.sh` for a complete debug build and `./scripts/i386/4-5-build-debug.sh` for an incremental debug build when unsure which build script to use.**

**Incremental debug build (i386):**
```bash
./scripts/i386/4-5-build-debug.sh             # Debug build
./scripts/i386/4-6-build-scheduling-debug.sh  # Scheduling debug logs : DON'T USE
```

**Build from scratch (i386):**
```bash
./scripts/i386/4-2-clean-build-debug.sh    # Clean then build debug
./scripts/i386/4-3-clean-build-scheduling-debug.sh # Clean then build with debug and scheduling debug logs : DON'T USE
```

**Run in QEMU (i386):**
```bash
./scripts/i386/5-1-start-qemu-ioapic-sata-e1000.sh  # Launches QEMU
./scripts/i386/5-2-debug-qemu-ioapic-sata-e1000.sh  # Launches QEMU with graphics and GDB
```

Replicate the same commands under `./scripts/x86-64/` when targeting the x86-64 architecture.

**Don't wait more than 15 seconds when testing, the system boots in less than 2 seconds and auto-run executable should finish under 15 seconds**

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
1. Use scheduling debug build when needing per-tick information, for scheduler/interrupt issues: `./scripts/i386/4-6-build-scheduling-debug.sh` (or `./scripts/i386/4-3-clean-build-scheduling-debug.sh` for a clean make) and their `./scripts/x86-64/` equivalents : GENERATES TONS OF LOG, USE WITH CARE.
2. Monitor `log/kernel.log` for exceptions and page faults
3. **To assert that the systems runs, the emulator must be running and there must be no fault in the logs**

**Disassembly Analysis:**
- `./scripts/utils/show-i386.sh <address> [context_lines]` (i386 build)
- `./scripts/utils/show-x86-64.sh <address> [context_lines]` (x86-64 build)
  - Default context: 20 lines before/after target address
  - Target line marked with `>>> ... <<<`
  - Example: `./scripts/utils/show-x86-64.sh 0xc0123456 5` (5 lines context)
