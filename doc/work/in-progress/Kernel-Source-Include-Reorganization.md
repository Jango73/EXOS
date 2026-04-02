# Kernel Source And Include Reorganization

## Goal

Reduce the number of files stored directly under `kernel/source` and `kernel/include` by moving them into responsibility-oriented folders that match the structure already present in the repository.

This proposal extends the existing layout instead of replacing it. The repository already uses coherent domains such as `arch`, `drivers`, `utils`, `process`, `desktop`, `network`, `shell`, `console`, `math`, `package`, `script`, and `input`. The main problem is the historical set of generic kernel files that still lives at the root level.

## Target Tree

```text
kernel/
├── include/
│   ├── arch/
│   ├── autotest/
│   ├── console/
│   ├── desktop/
│   ├── drivers/
│   ├── exec/
│   ├── fs/
│   ├── input/
│   ├── kernel/
│   ├── log/
│   ├── memory/
│   ├── network/
│   ├── package/
│   ├── process/
│   ├── runtime/
│   ├── script/
│   ├── shell/
│   ├── sync/
│   ├── system/
│   ├── text/
│   ├── ui/
│   ├── user/
│   └── utils/
└── source/
    ├── arch/
    ├── autotest/
    ├── console/
    ├── desktop/
    ├── drivers/
    ├── exec/
    ├── fs/
    ├── input/
    ├── kernel/
    ├── log/
    ├── memory/
    ├── network/
    ├── package/
    ├── process/
    ├── script/
    ├── shell/
    ├── sync/
    ├── system/
    ├── text/
    ├── ui/
    ├── user/
    └── utils/
```

## Proposed Mapping For Files Still At Root

### `kernel/`

Files that define the kernel core, boot entry, or generic registry-like services:

- `kernel/source/Main.c`
- `kernel/source/Kernel.c`
- `kernel/source/KernelData.c`
- `kernel/source/SystemDataView.c`
- `kernel/source/Device.c`
- `kernel/source/Driver.c`
- `kernel/source/DriverEnum.c`
- `kernel/include/Kernel.h`
- `kernel/include/KernelData.h`
- `kernel/include/Device.h`
- `kernel/include/Driver.h`
- `kernel/include/DriverEnum.h`
- `kernel/include/DriverGetters.h`
- `kernel/include/ID.h`

### `memory/`

Files that belong to memory management, allocators, and descriptor tracking:

- `kernel/source/Memory.c`
- `kernel/source/Memory-Descriptors.c`
- `kernel/source/Heap.c`
- `kernel/source/BuddyAllocator.c`
- `kernel/include/Memory.h`
- `kernel/include/Memory-Descriptors.h`
- `kernel/include/Heap.h`
- `kernel/include/BuddyAllocator.h`

### `sync/`

Files that implement kernel synchronization and deferred execution primitives:

- `kernel/source/Mutex.c`
- `kernel/source/DeferredWork.c`
- `kernel/include/Mutex.h`
- `kernel/include/DeferredWork.h`

### `exec/`

Files that describe executable formats and loading:

- `kernel/source/Executable.c`
- `kernel/source/ExecutableELF.c`
- `kernel/source/ExecutableEXOS.c`
- `kernel/include/Executable.h`
- `kernel/include/ExecutableELF.h`
- `kernel/include/ExecutableEXOS.h`
- `kernel/include/COFF.h`

### `fs/`

Files that expose the generic file system, file, disk, and root system filesystem layers:

- `kernel/source/File.c`
- `kernel/source/FileSystem.c`
- `kernel/source/SystemFS.c`
- `kernel/source/Disk.c`
- `kernel/include/File.h`
- `kernel/include/FileSystem.h`
- `kernel/include/SystemFS.h`
- `kernel/include/Disk.h`

### `log/`

Files that implement logging and profiling support:

- `kernel/source/Log.c`
- `kernel/source/Profile.c`
- `kernel/include/Log.h`
- `kernel/include/Profile.h`

### `text/`

Files that implement string handling, text rendering, localization, and text resources:

- `kernel/source/text/CoreString.c`
- `kernel/source/text/Quotes.c`
- `kernel/source/text/Text.c`
- `kernel/source/text/Lang.c`
- `kernel/include/text/CoreString.h`
- `kernel/include/text/Quotes.h`
- `kernel/include/text/Text.h`
- `kernel/include/text/Lang.h`

### `text/font/`

Files that implement fonts and embedded font data:

- `kernel/source/text/font/Font.c`
- `kernel/source/text/font/FontData-ASCII.c`
- `kernel/include/text/font/Font.h`

Target location after reorganization:

- `kernel/source/text/font/Font.c`
- `kernel/source/text/font/FontData-ASCII.c`
- `kernel/include/text/font/Font.h`

### `system/`

Files that expose system entry points, system calls, low-level timing, interrupts, and terminal-like base services:

- `kernel/source/SYSCall.c`
- `kernel/source/SYSCall-Graphics.c`
- `kernel/source/SYSCallTable.c`
- `kernel/source/Clock.c`
- `kernel/source/Interrupt.c`
- `kernel/source/SerialPort.c`
- `kernel/source/TTY.c`
- `kernel/include/SYSCall.h`
- `kernel/include/Clock.h`
- `kernel/include/Interrupt.h`
- `kernel/include/SerialPort.h`
- `kernel/include/System.h`

`TTY` may later move to `console/` if it becomes only a console concern, or to `fs/` if it is treated mainly as a filesystem-backed device node abstraction.

### `user/`

Files that define users, sessions, and identity-facing services:

- `kernel/source/user/UserAccount.c`
- `kernel/source/user/UserSession.c`
- `kernel/include/user/User.h`
- `kernel/include/user/UserAccount.h`
- `kernel/include/user/UserSession.h`
- `kernel/include/Security.h`

### `input/`

Files related to editing and interactive input helpers that do not belong to a specific hardware driver:

- `kernel/source/Edit-Main.c`
- `kernel/source/Edit-Input.c`
- `kernel/source/MemoryEditor.c`
- `kernel/include/Edit-Private.h`

`MemoryEditor.c` could also move to a dedicated diagnostic or shell tooling domain if it evolves into a debug-only component.

### `network/`

Some root-level network-related headers should move beside the existing network stack:

- `kernel/include/ARPContext.h`
- `kernel/include/Socket.h`

The matching implementation for `Socket` is already in `kernel/source/Socket.c`, which should also move into `kernel/source/network`.

### `autotest/`

Files for the autotest harness entry point:

- `kernel/source/Autotest.c`
- `kernel/include/Autotest.h`

## Headers That Should Likely Stay At Root

A small root-level base is still useful for truly fundamental headers that are included nearly everywhere:

- `kernel/include/Base.h`
- `kernel/include/Arch.h`
- `kernel/include/VarArg.h`
- `kernel/include/Endianness.h`

`vbr-multiboot.h` may also remain at root if it is primarily a boot-format definition rather than a reusable kernel subsystem header.

## Migration Strategy

Do not move everything at once. The safer approach is a staged migration that keeps the build working throughout the transition.

### Stage 1

Create the destination folders in `kernel/source` and `kernel/include`.

### Stage 2

Move implementation files first, domain by domain:

- `memory`
- `sync`
- `exec`
- `fs`
- `user`
- `system`
- `kernel`
- `log`

This keeps header include churn smaller while the implementation layout is stabilized.

### Stage 3

Move public headers to the matching folders.

### Stage 4

Add temporary compatibility forwarding headers at the old locations when needed. A forwarding header should stay minimal:

```c
#include "memory/Memory.h"
```

This allows the tree to be reorganized without forcing every include path to be rewritten in one patch.

### Stage 5

Update include paths incrementally by subsystem. Remove forwarding headers only after all users have been migrated.

### Stage 6

When a shared header changes, validate with clean builds as required by repository policy. If the header is cross-architecture or central, validate on both `x86-32` and `x86-64`.

## Rules For The Reorganization

- Avoid catch-all folders such as `core`, `common`, or `misc` unless there is a precise ownership rule.
- Keep dependencies unidirectional. Folder layout must not hide bidirectional subsystem coupling.
- Do not create local one-off helpers in a subsystem when the logic belongs in `kernel/include/utils` and `kernel/source/utils`.
- Keep driver-specific files in `drivers/`, even when they feel generic.
- Keep architecture-specific code in `arch/`, even when it interacts with generic services.
- Keep file sizes under repository limits while splitting by responsibility.

## Why This Layout Is Better

- It matches the subsystem split already visible in the repository.
- It removes the historical root-level dump without inventing an unrelated taxonomy.
- It gives each remaining root file a clear reason to exist.
- It allows progressive migration with compatibility headers instead of a disruptive rename-only patch.
- It makes ownership clearer when touching memory, execution, system, user, or kernel core code.

## Recommendation

Start with `memory`, `exec`, `fs`, and `user`. These domains are easy to name, already coherent, and should reduce root-level noise quickly without forcing deep architectural changes.
