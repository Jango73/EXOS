# EXOS Kernel documentation

## Table of contents
- [Table of contents](#table-of-contents)
- [Conventions](#conventions)
  - [Notations used in this document](#notations-used-in-this-document)
  - [Naming](#naming)
- [Platform and Memory Foundations](#platform-and-memory-foundations)
  - [Startup sequence on HD (real HD on x86-32 or qemu-system-i386)](#startup-sequence-on-hd-real-hd-on-x86-32-or-qemu-system-i386)
  - [Startup sequence on UEFI](#startup-sequence-on-uefi)
  - [Physical Memory map (may change)](#physical-memory-map-may-change)
  - [Paging abstractions](#paging-abstractions)
- [Isolation and Kernel Core](#isolation-and-kernel-core)
  - [Security Architecture](#security-architecture)
  - [Kernel objects](#kernel-objects)
  - [Handle reuse](#handle-reuse)
- [Execution Model and Kernel Interface](#execution-model-and-kernel-interface)
  - [Tasks](#tasks)
  - [Process and Task Lifecycle Management](#process-and-task-lifecycle-management)
  - [System calls](#system-calls)
  - [Task and window message delivery](#task-and-window-message-delivery)
  - [Command line editing](#command-line-editing)
  - [Exposed objects in shell](#exposed-objects-in-shell)
- [Hardware and Driver Stack](#hardware-and-driver-stack)
  - [Driver architecture](#driver-architecture)
  - [Input device stack](#input-device-stack)
  - [USB host and class stack](#usb-host-and-class-stack)
  - [Graphics and console paths](#graphics-and-console-paths)
  - [Early boot console path](#early-boot-console-path)
  - [ACPI services](#acpi-services)
  - [Disk interfaces](#disk-interfaces)
- [Storage and Filesystems](#storage-and-filesystems)
  - [File systems](#file-systems)
    - [Mounted volume naming](#mounted-volume-naming)
    - [EPK package format](#epk-package-format)
    - [Package namespace integration](#package-namespace-integration)
  - [EXOS File System - EXFS](#exos-file-system---exfs)
  - [Filesystem Cluster cache](#filesystem-cluster-cache)
  - [Foreign File systems](#foreign-file-systems)
- [Interaction and Networking](#interaction-and-networking)
  - [Shell scripting](#shell-scripting)
  - [Network Stack](#network-stack)
- [Tooling and References](#tooling-and-references)
  - [Logging](#logging)
  - [Automated debug validation script](#automated-debug-validation-script)
  - [Build output layout](#build-output-layout)
  - [Package tooling](#package-tooling)
  - [Keyboard Layout Format (EKM1)](#keyboard-layout-format-ekm1)
  - [QEMU network graph](#qemu-network-graph)
  - [Links](#links)
## Conventions

### Notations used in this document

| Abbrev | Meaning                         |
|--------|---------------------------------|
| U8     | unsigned byte                   |
| I8     | signed byte                     |
| U16    | unsigned word                   |
| I16    | signed word                     |
| U32    | unsigned long                   |
| I32    | signed long                     |
| U64    | unsigned qword                  |
| I64    | signed qword                    |
| UINT   | unsigned register-sized integer |
| INT    | signed register-sized integer   |

| Abbrev | Meaning                           |
|--------|-----------------------------------|
| EXOS   | Extensible Operating System       |
| BIOS   | Basic Input/Output System         |
| CHS    | Cylinder-Head-Sector              |
| MBR    | Master Boot Record                |
| OS     | Operating System                  |

---


### Naming

The following naming conventions have been adopted throughout the EXOS code base and interface.

- Structure : SCREAMING_SNAKE_CASE
- Macro : SCREAMING_SNAKE_CASE
- Function : PascalCase
- Variable : PascalCase
- Shell command : lower_snake_case
- Shell object/property : lower_snake_case


## Platform and Memory Foundations

### Startup sequence on HD (real HD on x86-32 or qemu-system-i386)

Everything in this sequence runs in 16-bit real mode on x86-32 processors. However, the code uses 32 bit registers when appropriate.

1. BIOS loads disk MBR at 0x7C00.
2. Code in mbr.asm is executed.
3. MBR code looks for the active partition and loads its VBR at 0x7E00.
4. Code in vbr.asm is executed.
5. VBR code loads the reserved FAT32/EXT2 sectors (which contain VBR payload) at 0x8000.
6. Code in vbr-payload-a.asm is executed.
7. VBR payload asm sets up a stack and calls BootMain in vbr-payload-c.c.
8. BootMain finds the FAT32/EXT2 entry for the specified binary.
9. BootMain reads all the clusters of the binary at 0x20000.
10. EnterProtectedPagingAndJump sets up minimal GDT and paging structures for the loaded binary to execute in higher half memory (0xC0000000).
11. It finally jumps to the loaded binary.
12. That's all folks. But it was a real pain to code :D


### Startup sequence on UEFI

1. Firmware loads `EFI/BOOT/BOOTX64.EFI` (x86-64) or `EFI/BOOT/BOOTIA32.EFI` (x86-32) from the EFI System Partition (FAT32).
2. The UEFI loader reads `exos.bin` from the root folder of the EFI System Partition into physical address 0x200000.
3. The loader gathers the UEFI memory map, converts it to an E820 map, and builds the Multiboot information block. The first module descriptor carries the loader-reserved kernel span size in `module.reserved`; this value is mandatory for EXOS and consumed directly by the kernel memory initialization.
4. The loader switches to EXOS paging and GDT layout, then jumps to the kernel entry with the Multiboot registers set.


### Physical Memory map on x86-32 (may change)

```
┌──────────────────────────────────────────────────────────────────────────┐
│ 00000000 -> 000003FF  IVT                                                │
├──────────────────────────────────────────────────────────────────────────┤
│ 00000400 -> 000004FF  BIOS Data                                          │
├──────────────────────────────────────────────────────────────────────────┤
│ 00000500 -> 00000FFF  ??                                                 │
├──────────────────────────────────────────────────────────────────────────┤
│ 00002000 -> 00004FFF  VBR memory                                         │
├──────────────────────────────────────────────────────────────────────────┤
│ 00005000 -> 0001FFFF  Unused                                             │
├──────────────────────────────────────────────────────────────────────────┤
│ 00020000 -> 0009FBFF  EXOS Kernel (523263 bytes) mapped at C0000000      │
├──────────────────────────────────────────────────────────────────────────┤
│ 0009FC00 -> 0009FFFF  Extended BIOS data area                            │
├──────────────────────────────────────────────────────────────────────────┤
│ 000A0000 -> 000B7FFF  ROM Reserved                                       │
├──────────────────────────────────────────────────────────────────────────┤
│ 000B8000 -> 000BFFFF  Console buffer                                     │
├──────────────────────────────────────────────────────────────────────────┤
│ 000C0000 -> 000CFFFF  VESA                                               │
├──────────────────────────────────────────────────────────────────────────┤
│ 000F0000 -> 000FFFFF  BIOS                                               │
├──────────────────────────────────────────────────────────────────────────┤
│ 00100000 -> 003FFFFF  ??                                                 │
├──────────────────────────────────────────────────────────────────────────┤
│ 00400000 -> EFFFFFFF  Flat free RAM                                      │
├──────────────────────────────────────────────────────────────────────────┤
```


### Paging abstractions

The memory subsystem is split into architecture-agnostic services and architecture backends.

#### Layering and architecture backend

Common memory code consumes helpers exposed by `arch/Memory.h` and does not manipulate paging bitfields directly. Backend headers expose entry builders and accessors such as `MakePageDirectoryEntryValue`, `MakePageTableEntryValue`, and `WritePage*EntryValue`, which keeps common allocation logic independent from paging depth.

On x86-32, `arch/x86-32/x86-32-Memory.h` also exposes self-map helpers and an `ARCH_PAGE_ITERATOR`. Region routines (`IsRegionFree`, `AllocRegion`, `FreeRegion`, `ResizeRegion`) walk mappings through this iterator and reclaim empty page tables through `PageTableIsEmpty`. Physical range clipping is delegated to `ClipPhysicalRange`.

#### Physical memory allocation (buddy allocator)

Each architecture owns the low-level `InitializeMemoryManager` sequence. Both x86-32 and x86-64 bootstrap buddy metadata in low mapped memory, register loader-reserved and allocator-metadata physical spans (`SetLoaderReservedPhysicalRange`, `SetPhysicalAllocatorMetadataRange`), then call `MarkUsedPhysicalMemory` to reserve low memory, loader pages, allocator metadata pages, and non-available multiboot ranges before normal allocations start.

This separates physical page management (buddy allocator) from virtual mapping bookkeeping.

#### Virtual address space construction

Virtual mappings are managed through region APIs (`AllocRegion`, `FreeRegion`, `ResizeRegion`) that populate page tables and then flush translation state.

On x86-32 bootstrap, the page directory maps the TaskRunner page with write access so the kernel main stack can execute during early bring-up.

On x86-64, `AllocPageDirectory` builds fresh paging structures (low-memory region, kernel region, recursive slot, task-runner window) instead of cloning loader tables. `AllocUserPageDirectory` reuses these helpers and pre-installs a userland seed table so user mappings can be populated immediately. The default x86-64 kernel virtual base (`VMA_KERNEL`) is `0xFFFFFFFFC0000000`.

#### Region descriptor tracking

Both x86-32 and x86-64 track successful virtual region operations with `MEMORY_REGION_DESCRIPTOR` records linked from `PROCESS.RegionListHead`. Allocation uses `RegionTrackAlloc`, release uses `RegionTrackFree`, and growth/shrink uses `RegionTrackResize`.

Descriptors are allocated from dedicated descriptor slabs mapped with `AllocKernelRegion`, so descriptor metadata does not consume the process heap. Each descriptor stores canonical base, size/page count, optional physical origin, flags/attributes, tag, and paging granularity.

Descriptor slab bootstrap is protected by `G_RegionDescriptorBootstrap`. While descriptor slabs are being allocated and mapped, tracking callbacks are temporarily bypassed to prevent recursive descriptor allocation.


## Isolation and Kernel Core

### Security Architecture

Security in EXOS is implemented as a layered architecture. The effective access decision is the result of CPU privilege isolation, virtual memory boundaries, session-backed user identity, syscall privilege checks, and object-level policy inside subsystems.

#### Layer 1: CPU privilege domains and execution context

- EXOS uses kernel/user CPU privilege separation (ring 0 and ring 3) in both x86-32 and x86-64 task setup paths.
- Task setup selects kernel or user code/data selectors based on `Process->Privilege` and seeds the initial interrupt frame accordingly (`kernel/source/arch/x86-32/x86-32.c`, `kernel/source/arch/x86-64/x86-64.c`).
- Kernel tasks start at `TaskRunner` with kernel selectors; user tasks start through the user-mapped task runner trampoline with user selectors.
- On x86-64, task setup also allocates a dedicated IST1 stack for fault handling, reducing the risk of stack-corruption escalation during exceptions.

#### Layer 2: Virtual memory isolation

- Per-process address spaces are built with separate kernel and user privilege page mappings.
- Kernel mappings are created with kernel page privilege; user seed tables and task runner mappings are created with user page privilege (`kernel/source/arch/x86-32/x86-32-Memory.c`, `kernel/source/arch/x86-64/x86-64-Memory-HighLevel.c`).
- User pointers received from syscalls are validated through `SAFE_USE_VALID`, `SAFE_USE_INPUT_POINTER`, and `IsValidMemory` checks before dereference (`kernel/source/SYSCall.c`).

#### Layer 3: Identity and session model

- Identity is session-centric: `GetCurrentUser()` resolves the current process session to a user account (`kernel/source/utils/Helpers.c`).
- `USERACCOUNT` stores `UserID`, privilege (`EXOS_PRIVILEGE_USER` or `EXOS_PRIVILEGE_ADMIN`), status, and password hash; `USERSESSION` stores `SessionID`, `UserID`, login/activity timestamps, lock state, and shell task binding (`kernel/include/UserAccount.h`).
- Session lifecycle is managed by `CreateUserSession`, `SetCurrentSession`, `GetCurrentSession`, timeout validation, and lock/unlock helpers in `UserSession.c`.
- Session inactivity timeout is configurable with `Session.TimeoutSeconds` in kernel configuration, with a compile fallback to `SESSION_TIMEOUT_MS`. Key `Session.TimeoutMinutes` is also accepted when `Session.TimeoutSeconds` is absent.
- Child process creation inherits the parent session (`Process->Session`), preserving identity continuity across spawned processes (`kernel/source/process/Process.c`).

#### Layer 4: Syscall privilege gate

- Every syscall is dispatched through `SystemCallHandler`, which checks the required privilege stored in `SysCallTable[]` before calling the handler (`kernel/source/SYSCall.c`, `kernel/source/SYSCallTable.c`).
- The privilege model is ordinal (`kernel < admin < user`) and compares current user privilege against the required level.
- Authentication/user-management syscalls (`Login`, `Logout`, `CreateUser`, `DeleteUser`, `ListUsers`, `ChangePassword`) apply explicit account checks in their handlers (`kernel/source/SYSCall.c`).

#### Layer 5: Handle boundary and kernel object exposure

- User space passes opaque handles; the kernel translates with `HandleToPointer`/`PointerToHandle` and validates target object type before operation (`kernel/source/SYSCall.c`).
- The shell script exposure layer enforces read policy per object and per field using `EXPOSE_REQUIRE_ACCESS(...)` with flags:
  - `EXPOSE_ACCESS_PUBLIC`
  - `EXPOSE_ACCESS_SAME_USER`
  - `EXPOSE_ACCESS_ADMIN`
  - `EXPOSE_ACCESS_KERNEL`
  - `EXPOSE_ACCESS_OWNER_PROCESS`
  (defined in `kernel/include/Exposed.h`, implemented by `kernel/source/expose/Expose-Security.c`)
- Process/task exposure protects sensitive fields (`page_directory`, heap metadata, architecture context, stack internals) behind kernel/admin or owner-process checks (`kernel/source/expose/Expose-Process.c`, `kernel/source/expose/Expose-Task.c`).

#### Layer 6: Security data model for kernel objects

- `SECURITY` objects provide owner and per-user permission fields (`READ`, `WRITE`, `EXECUTE`) with default permissions (`kernel/include/Security.h`).
- Process structures embed a `SECURITY` instance initialized by `InitSecurity()` during process creation (`kernel/source/process/Process.c`).
- This data model is present and initialized, while policy enforcement is currently concentrated in syscall handlers and expose-layer checks.

#### Architectural properties and current boundaries

- The architecture provides defense-in-depth through independent barriers (CPU ring separation, page privilege separation, session identity, syscall gate, and field-level exposure checks).
- Access control for script-visible kernel state is fine-grained and centralized through reusable expose security helpers.
- Security policy is not represented as a single global ACL engine. Some controls are explicit per-subsystem (for example, admin checks in user-management syscalls), which keeps behavior clear but requires discipline when adding new kernel entry points.


### Kernel objects

#### Object identifiers

Every kernel object stores a 64-bit identifier in `OBJECT_FIELDS`. The identifier is assigned by `CreateKernelObject` using a random UUID source and is kept for the object lifetime, including in the termination cache through `OBJECT_TERMINATION_STATE.ID`. This provides stable identity when memory is reused and keeps scheduler lookups independent from raw pointers. Any kernel object that contains `OBJECT_FIELDS` (and therefore `LISTNODE_FIELDS`) and is meant to live in a global kernel list must be created with `CreateKernelObject` and destroyed with `ReleaseKernelObject`.

#### Event objects

Kernel events (`KOID_KERNELEVENT`) are lightweight objects for ISR-to-task signaling, implemented in `kernel/source/KernelEvent.c`. They embed `LISTNODE_FIELDS` plus a `Signaled` flag and `SignalCount` counter, and are tracked in `Kernel.Event` alongside other kernel-managed lists so reference tracking and garbage collection treat them uniformly.

Event lifecycle and state helpers:

- `CreateKernelEvent()` and `DeleteKernelEvent()` manage allocation and reference counting.
- `SignalKernelEvent()` and `ResetKernelEvent()` update the signaled state with local interrupts masked (`SaveFlags`/`DisableInterrupts`) so they are safe in interrupt context.
- `KernelEventIsSignaled()` and `KernelEventGetSignalCount()` expose state for scheduler and consumer code.

`Wait()` recognizes event handles in `WAITINFO.Objects` and returns when `SignalKernelEvent` marks the event signaled, propagating `SignalCount` through the wait exit codes. The termination cache remains the first check for process and task exits to preserve legacy behavior.

#### List nodes

Kernel objects that embed `LISTNODE_FIELDS` participate in intrusive lists. Each list node carries a `Parent` pointer so objects can represent hierarchy when required, but insertion helpers keep the `Parent` pointer NULL unless it is explicitly set by the caller. This avoids accidental parent chains while still enabling structured ownership models.


### Handle reuse

#### Global mapping architecture

Handle translation is centralized in one global `HANDLE_MAP` stored in `Kernel.HandleMap` and initialized during `InitializeKernel()` with `HandleMapInit()`.

`HANDLE_MAP` combines:
- a radix tree (`Map->Tree`) keyed by handle value,
- a slab-style block allocator (`Map->EntryAllocator`) for `HANDLE_MAP_ENTRY`,
- a mutex (`Map->Mutex`) guarding every map operation,
- a monotonic allocator cursor (`Map->NextHandle`), starting at `HANDLE_MINIMUM` (`0x10`).

Each map entry stores `{Handle, Pointer, Attached}`. A handle is considered valid for resolution only when `Attached` is true and `Pointer` is non-null.

#### Conversion flow

`PointerToHandle()` enforces pointer-to-handle reuse before allocation:
1. Reject null pointers.
2. Search for an existing mapping with `HandleMapFindHandleByPointer()`.
3. Reuse the existing handle when found.
4. Otherwise allocate a new handle (`HandleMapAllocateHandle`) and attach the pointer (`HandleMapAttachPointer`).

This guarantees one active exported handle per pointer in the global map and avoids duplicate exports during repeated conversions.

`HandleToPointer()` performs the reverse operation through `HandleMapResolveHandle()`. `ReleaseHandle()` detaches the pointer (`HandleMapDetachPointer`) then removes the handle entry (`HandleMapReleaseHandle`).

`EnsureKernelPointer()` and `EnsureHandle()` normalize mixed values in call paths that may receive either raw kernel pointers or user-visible handles.

#### Message path behavior

`SysCall_GetMessage()` and `SysCall_PeekMessage()` convert `MESSAGEINFO.Target` from handle to pointer before calling the internal queue logic, then convert the returned pointer back through `PointerToHandle()`. `SysCall_DispatchMessage()` resolves the incoming handle to a pointer for dispatch, then restores the original handle in the user buffer.

Because `PointerToHandle()` reuses existing mappings, repeated message retrieval and dispatch cycles keep target handles stable instead of generating transient replacements.

#### Complexity boundary

Forward resolution (handle -> pointer) is radix-tree lookup. Reverse lookup (pointer -> handle reuse check) is implemented by iterating the radix tree (`HandleMapFindHandleByPointer`), so it is linear in the number of active handles. The design favors simple global consistency and stable handle identity over constant-time reverse indexing.


## Execution Model and Kernel Interface

### Tasks

#### Architecture-specific task data

Each task embeds an `ARCH_TASK_DATA` structure (declared in the architecture-specific header under `kernel/include/arch/`) that contains the saved interrupt frame along with the user, system, and any auxiliary stack descriptors that the target CPU requires. The generic `tag_TASK` definition in `kernel/include/process/Task.h` exposes this structure as the `Arch` member so that all stack and context manipulations remain scoped to the active architecture.

The x86-32 implementation of `SetupTask` (`kernel/source/arch/x86-32/x86-32.c`) is responsible for allocating and clearing the per-task stacks, initialising the selectors in the interrupt frame and performing the bootstrap stack switch for the main kernel task. The x86-64 flavour performs the same duties and additionally provisions a dedicated Interrupt Stack Table (IST1) stack for faults that require a reliable kernel stack even if the regular system stack becomes unusable. During IDT initialisation the kernel assigns IST1 to the fault vectors that are most likely to execute with a corrupted task stack (double fault, invalid TSS, segment-not-present, stack, general protection and page faults). This ensures the handlers always run on the emergency per-task stack, preventing the double-fault escalation that previously produced a triple fault when the active stack pointer was already invalid. `CreateTask` calls the relevant helper after finishing the generic bookkeeping, which keeps the scheduler and task manager architecture-agnostic while allowing future architectures to provide their own `SetupTask` specialisation.

Both the x86-32 and x86-64 context-switch helpers (`SetupStackForKernelMode` and `SetupStackForUserMode` in their respective architecture headers) must reserve space on the stack in bytes rather than entries before writing the return frame. Subtracting the correct byte count avoids writing past the top of the allocated stack when seeding the initial `iret` frame for a task. On x86-64 the helpers also arrange the bootstrap frame so that the stack pointer becomes 16-byte aligned after `iretq` pops its arguments, preserving the ABI-mandated alignment once execution resumes in the scheduled task.

#### Stack sizing

The minimum sizes for task and system stacks are driven by the configuration keys `Task.MinimumTaskStackSize` and `Task.MinimumSystemStackSize` in `kernel/configuration/exos.ref.toml`. At boot the task manager reads those values, but it clamps them to the architecture defaults (`64 KiB`/`16 KiB` on x86-32 and `128 KiB`/`32 KiB` on x86-64) to prevent under-provisioned stacks. Increasing the values in the configuration grows every newly created task and keeps the auto stack growing logic operating on the larger baseline.

Stack growth also enforces compile-time caps defined in `kernel/include/Stack.h` (`STACK_MAXIMUM_TASK_STACK_SIZE`, `STACK_MAXIMUM_SYSTEM_STACK_SIZE`). The kernel does not rely on runtime configuration for these hard limits.

When an in-place resize fails (for example because the next virtual range is occupied), `GrowCurrentStack` relocates the active stack to a new region, switches the live stack pointer to the new top, updates the task stack descriptor, then releases the old region. This keeps kernel stack growth functional even when neighboring mappings block contiguous expansion.

On x86-64, task setup allocates IST1 with an explicit guard gap above the system stack, so emergency fault stack placement does not sit immediately adjacent to the regular system stack.

The stack autotest module (`TestCopyStack`) is registered for on-demand execution only. It is excluded from the boot-time `RunAllTests` path and can be triggered manually from the shell with `autotest stack`.

#### IRQ scheduling

##### IRQ 0 path

IRQ 0 └── trap lands in interrupt-a.asm : Interrupt_Clock
    └── calls ClockHandler to increment system time
    └── calls Scheduler to check if it's time to switch to another task
        └── Scheduler switches page directory if needed and returns the next task's context

##### ISR 0 call graph

```
Interrupt_Clock └── BuildInterruptFrame
    └── KernelLogText
        └── StringEmpty
        └── StringPrintFormatArgs
            └── IsNumeric : endpoint
            └── IsNumeric : endpoint
            └── SkipAToI : endpoint
            └── VarArg : endpoint
            └── StringLength : endpoint
            └── NumberToString : endpoint
        └── KernelPrintString
            └── LockMutex
                └── SaveFlags : endpoint
                └── DisableInterrupts : endpoint
                └── GetCurrentTask : endpoint
                └── RestoreFlags : endpoint
                └── GetSystemTime : endpoint
                └── IdleCPU : endpoint
            └── UnlockMutex
                └── SaveFlags : endpoint
                └── DisableInterrupts : endpoint
                └── GetCurrentTask : endpoint
                └── RestoreFlags : endpoint
        └── KernelPrintChar : endpoint
└── ClockHandler
    └── KernelLogText
        └── ...
    └── KernelPrintString
        └── ...
    └── IsLeapYear : endpoint
    └── Scheduler
        └── KernelLogText
            └── ...
        └── CheckStack
            └── GetCurrentTask : endpoint
        └── KillTask
            └── KernelLogText
                └── ...
            └── RemoveTaskFromQueue
                └── FreezeScheduler
                    └── LockMutex
                        └── ...
                    └── UnlockMutex
                        └── ...
                └── FindNextRunnableTask
                    └── GetSystemTime : endpoint
                └── UnfreezeScheduler
                    └── LockMutex
                        └── ...
                    └── UnlockMutex
                        └── ...
                └── KernelLogText
                    └── ...
            └── LockMutex
                └── ...
            └── ListRemove : endpoint
            └── DeleteTask
                └── KernelLogText
                    └── ...
                    └── HeapFree_HBHS : endpoint
                    └── HeapFree
                        └── GetCurrentProcess
                            └── GetCurrentTask : endpoint
                        └── HeapFree_P
                            └── LockMutex
                                └── ...
                            └── HeapFree_HBHS : endpoint
                            └── UnlockMutex
                                └── ...
            └── UnlockMutex
                └── ...
```


### Process and Task Lifecycle Management

EXOS implements a lifecycle management system for both processes and tasks that ensures consistent cleanup and prevents resource leaks.

#### Process Heap Management

- Every `PROCESS` keeps track of its `MaximumAllocatedMemory`, which is initialized to `N_HalfMemory` for both the kernel and user processes.
- When a heap allocation exhausts the committed region, the kernel automatically attempts to double the heap size without exceeding the process limit by calling `ResizeRegion`.
- If the resize operation cannot be completed, the allocator logs an error and the allocation fails gracefully.
- Kernel heap allocations that still fail dump the current task interrupt frame through the same logging path used by the #GP/#PF handlers, giving register and backtrace context when diagnosing out-of-heap issues.

#### Status States

**Task Status (Task.Status):**
- `TASK_STATUS_FREE` (0x00): Unused task slot
- `TASK_STATUS_READY` (0x01): Ready to run
- `TASK_STATUS_RUNNING` (0x02): Currently executing
- `TASK_STATUS_WAITING` (0x03): Waiting for an event
- `TASK_STATUS_SLEEPING` (0x04): Sleeping for a specific time
- `TASK_STATUS_WAITMESSAGE` (0x05): Waiting for a message
- `TASK_STATUS_DEAD` (0xFF): Marked for deletion

- Sleep durations are specified in `UINT`. A value of `INFINITY` is treated as a sentinel meaning "sleep indefinitely". `SetTaskWakeUpTime()` stores `INFINITY` without adding the current time and the scheduler ignores such tasks until another subsystem explicitly changes their status.

**Process Status (Process.Status):**
- `PROCESS_STATUS_ALIVE` (0x00): Normal operating state
- `PROCESS_STATUS_DEAD` (0xFF): Marked for deletion

#### Process Creation Flags

**Process Creation Flags (Process.Flags):**
- `PROCESS_CREATE_TERMINATE_CHILD_PROCESSES_ON_DEATH` (0x00000001): When the process terminates, all child processes are also killed. If this flag is not set, child processes are orphaned (their Parent field is set to NULL).

#### Session Inheritance

- New processes inherit the user session pointer from their `OwnerProcess` during `NewProcess`.
- Session ownership is therefore tied to the process tree: children share the same session by default unless explicitly reassigned.
- This keeps user identity and security context consistent across a spawned process hierarchy.

#### Lifecycle Flow

**1. Task Termination:**
- When a task terminates, `KillTask()` releases every mutex held by the task before marking it as `TASK_STATUS_DEAD`
- The task remains in the scheduler queue until the next context switch
- `DeleteDeadTasksAndProcesses()` (called periodically) removes dead tasks and processes from lists

**2. Process Termination via Task Count:**
- When `DeleteTask()` processes a dead task:
  - Decrements `Process.TaskCount`
  - If `TaskCount` reaches 0:
    - Applies child process policy based on `PROCESS_CREATE_TERMINATE_CHILD_PROCESSES_ON_DEATH` flag
    - Marks the process as `PROCESS_STATUS_DEAD`
  - The process remains in the process list for later cleanup

**3. Process Termination via KillProcess:**
- `KillProcess()` can be called to terminate a process and handle its children:
  - Checks the `PROCESS_CREATE_TERMINATE_CHILD_PROCESSES_ON_DEATH` flag
  - If flag is set: Finds all child processes recursively and kills them
  - If flag is not set: Orphans children by setting their `Parent` field to NULL
  - Calls `KillTask()` on all tasks of the target process
  - Marks the target process as `PROCESS_STATUS_DEAD`

**4. Final Cleanup:**
- `DeleteDeadTasksAndProcesses()` is called periodically by the kernel monitor
- First phase: Processes all `TASK_STATUS_DEAD` tasks
  - Calls `DeleteTask()` which frees stacks, message queues, etc.
  - Updates process task counts and marks processes dead if needed
- Second phase: Processes all `PROCESS_STATUS_DEAD` processes
  - Calls `ReleaseProcessKernelObjects()` to drop references held by the process on every kernel-managed list
  - Calls `DeleteProcessCommit()` which frees page directories, heaps, etc.
  - Removes process from global process list

#### Key Design Principles

**Deferred Deletion:**
- Neither tasks nor processes are immediately freed when killed
- They are marked as DEAD and cleaned up later by `DeleteDeadTasksAndProcesses()`
- This prevents race conditions and ensures consistent state

**Hierarchical Process Management:**
- Child process handling depends on parent's `PROCESS_CREATE_TERMINATE_CHILD_PROCESSES_ON_DEATH` flag
- If flag is set: Child processes are automatically killed when parent dies
- If flag is not set: Child processes are orphaned (Parent set to NULL)
- The `Parent` field creates a process tree structure
- `KillProcess()` implements policy-based child handling

**Mutex Protection:**
- Process list operations are protected by `MUTEX_PROCESS`
- Task list operations are protected by `MUTEX_KERNEL`
- `ReleaseProcessKernelObjects()` requires `MUTEX_KERNEL` to be locked while iterating kernel lists
- Task count updates are atomic to prevent race conditions

**Resource Cleanup Order:**
1. Tasks are killed first (marked as DEAD)
2. Task resources are freed (stacks, message queues)
3. Process task count reaches zero
4. Process is marked as DEAD
5. Process resources are freed (page directory, heap)
6. Process is removed from global list

This approach ensures that:
- No resources are leaked during process/task termination
- Parent-child relationships are properly maintained
- The system remains stable during complex termination scenarios
- Both voluntary (task exit) and involuntary (kill) termination work consistently


### System calls

#### System call full path - x86-32

```
exos-runtime-c.c : malloc() (or any other function)
└── calls exos-runtime-a.asm : exoscall()
    └── 'int EXOS_USER_CALL' instruction
        └── trap lands in interrupt-a.asm : Interrupt_SystemCall
            └── calls SYSCall.c : SystemCallHandler()
                └── calls SysCall_xxx via SysCallTable[]
                    └── whew... finally job is done
```

#### System call full path - x86-64

When `USE_SYSCALL = 0` (default build setting)
```
exos-runtime-c.c : malloc() (or any other function)
└── calls exos-runtime-a.asm : exoscall()
    └── 'int EXOS_USER_CALL' instruction
        └── trap lands in interrupt-a.asm : Interrupt_SystemCall
            └── calls SYSCall.c : SystemCallHandler()
                └── calls SysCall_xxx via SysCallTable[]
                    └── whew... finally job is done
```

When `USE_SYSCALL = 1`
```
exos-runtime-c.c : malloc() (or any other function)
└── calls exos-runtime-a.asm : exoscall()
    └── 'syscall' instruction
        └── syscall lands in interrupt-a.asm : Interrupt_SystemCall
            └── calls SYSCall.c : SystemCallHandler()
                └── calls SysCall_xxx via SysCallTable[]
                    └── whew... finally job is done
```

`USE_SYSCALL` is a project-level build flag (`./scripts/build --arch x86-64 --fs ext2 --debug --use-syscall`) that selects between the legacy interrupt gate and the SYSCALL/SYSRET pair on x86-64. The flag has no effect on x86-32 builds.

`SYSTEM_DATA_VIEW` is a project-level build flag (`./scripts/build --arch x86-32 --fs ext2 --system-data-view`) that enables the System Data View mode before task creation. The mode shows the system data pages, uses the kernel keyboard input for navigation (left/right to change page, up/down to scroll), and exits on `Esc` to continue boot. The xHCI page reports PCI identity, decoded PCI status error flags, scratchpad capability/state (`HCSPARAMS2`, `MaxScratchpadBuffers`, `DCBAA[0]`), controller runtime registers (`USBCMD`, `USBSTS`, `CRCR`, `DCBAAP`, interrupter state), slot usage, and per-root-port enumeration diagnostics (raw `PORTSC`, speed/link state, last enumeration error/completion, present/slot state).


### Task and window message delivery

Tasks own a lazily instantiated message queue (`MESSAGEQUEUE` in `kernel/source/process/TaskMessaging.c`) built on the generic list container. Only the kernel process starts with a queue; user processes and their tasks get a queue *only when they explicitly call* `GetMessage()`, `PeekMessage()`, or `WaitForMessage()` (which marks the task queue as initialized). No queue is created when posting; if a task/process never asked for one, posted messages are dropped and keyboard input continues down the classic buffered path for `getkey()` (used by the shell). When a process message queue exists, the keyboard helpers (`PeekChar`, `GetChar`, `GetKeyCode`) consume key events from that queue by discarding `EWM_KEYUP` messages and returning the first `EWM_KEYDOWN`, then fall back to the classic buffer when no queue exists. Each queue is capped to 100 pending messages and guarded by a per-queue mutex plus a waiting flag. `WaitForMessage` marks the queue as waiting and sleeps the task; `AddTaskMessage` wakes the task when a new message arrives and clears the waiting flag.

Message posting:
- `PostMessage` accepts NULL targets (current task), task handles, and window handles; window targets enqueue into the owning task queue. Keyboard drivers and the mouse dispatcher push input events into the global input queue using `EnqueueInputMessage` so only the focused process sees them.
- Mouse input is throttled by a tiny dispatcher that filters `EWM_MOUSEMOVE` with a 10ms cooldown between enqueues, while button changes still dispatch immediately through the shared input queue.
- `SendMessage` remains synchronous and window-only.

Message retrieval:
- `GetMessage`/`PeekMessage` first check the global input queue when the caller’s process has focus (desktop focus + per-desktop `FocusedProcess`), then fall back to the task’s own queue. `GetMessage` blocks if neither queue holds messages; `PeekMessage` is non-blocking. Userland syscalls translate handles in `MESSAGEINFO` before dispatching to the kernel implementations.
- Focus tracking lives in `Kernel.FocusedDesktop` and `Desktop.FocusedProcess`. When a process is created on the focused desktop it becomes the focused process; when a focused process dies its desktop falls back to the kernel process. The focus setters ensure a focused process always exists for the active desktop.


### Command line editing

Interactive editing of shell command lines is implemented in `kernel/source/utils/CommandLineEditor.c`. The module processes keyboard input via the classic buffered path (`PeekChar`/`GetKeyCode`), maintains an in-memory history, refreshes the console display, and relies on callbacks to retrieve completion suggestions. The shell owns an input state structure that embeds the editor instance and provides shell-specific callbacks for completion and idle processing so the component remains agnostic of higher level shell logic. While reading input, the editor adjusts for console scrolling so the display does not re-trigger scrolling on each key press, console paging prompts are suspended until the line is submitted, and successful key interactions update session activity timestamps.

Keyboard input keeps two distinct paths for compatibility. The legacy PS/2 pipeline continues to use scan code -> KEYTRANS tables, while a separate HID path uses usage page 0x07 indexed KEY_LAYOUT_HID layouts. The HID layout file format is UTF-8 text with an "EKM1" header and directives: code, levels, map, dead, and compose. The kernel keeps an embedded en-US fallback (KEY_LAYOUT_FALLBACK_CODE) used when HID layout loading fails. The HID layout loader parses EKM1 files with a tolerant UTF-8 decoder, logs replacement counts, and rejects malformed directives or out-of-range entries. USB HID keyboard support lives in `kernel/source/drivers/input/Keyboard-USB.c`; keyboard reports are processed in boot protocol on the primary keyboard interface, and consumer/media usages (usage page 0x0C) are decoded from an optional secondary HID consumer interface into the common key event path with keydown/keyup transitions. HID report descriptor decoding is implemented by the reusable helper `utils/HIDReport` (`kernel/include/utils/HIDReport.h`, `kernel/source/utils/HIDReport.c`). Keyboard initialization is mediated by a selector driver (`kernel/source/drivers/input/Keyboard-Selector.c`) that probes for a USB HID keyboard after PCI/xHCI enumeration and otherwise falls back to PS/2 detection, ensuring only one keyboard driver is active at a time.

All reusable helpers -such as the command line editor, adaptive delay, string containers, byte-size formatting helpers (`utils/SizeFormat`), CRC/SHA-256 utilities, compression utilities, chunk cache utilities, detached signature utilities, notifications, path helpers, TOML parsing, UUID support, regex, hysteresis control, cooldown timing, rate limiting, and network checksum helpers— live under `kernel/source/utils` with their public headers in `kernel/include/utils`. SHA-256 is exposed through `utils/Crypt` and bridged to the vendored BearSSL hash implementation under `third/bearssl`. Compression is exposed through `utils/Compression` and bridged to the vendored miniz backend under `third/miniz`. Detached signature verification is exposed through `utils/Signature` with a backend-swappable API surface, and Ed25519 verification is wired to vendored Monocypher sources under `third/monocypher`. This keeps generic infrastructure separated from core subsystems and makes it easier to share common code across the kernel.


### Exposed objects in shell

- `process`: Kernel process list root. provides indexed access to process views. Permissions: exposed to scripts; access is enforced per item and per field.
  - `process.count`: total number of processes. Permissions: anyone.
  - `process[n]`: process view at index `n`. Permissions: see fields below.
    - `handle`: handle for the process. Permissions: anyone (except `process[0]`, kernel and administrator only).
    - `status`: current process status. Permissions: anyone (except `process[0]`, kernel and administrator only).
    - `flags`: process flags bitfield. Permissions: anyone (except `process[0]`, kernel and administrator only).
    - `exit_code`: termination status. Permissions: anyone (except `process[0]`, kernel and administrator only).
    - `file_name`: executable name. Permissions: anyone (except `process[0]`, kernel and administrator only).
    - `command_line`: full command line string. Permissions: anyone (except `process[0]`, kernel and administrator only).
    - `work_folder`: current working folder. Permissions: anyone (except `process[0]`, kernel and administrator only).
    - `page_directory`: page directory pointer. Permissions: kernel and administrator only.
    - `heap_base`: heap base address. Permissions: kernel and administrator only.
    - `heap_size`: heap size. Permissions: kernel and administrator only.
    - `task`: task list for the owning process. Permissions: kernel and administrator only.
      - `task.count`: number of tasks in the process. Permissions: kernel and administrator only.
      - `task[n]`: task view at index `n`. Permissions: see fields below.
        - `handle`: handle for the task. Permissions: anyone (except tasks that belong to the kernel process, kernel and administrator only).
        - `name`: task name. Permissions: anyone (except tasks that belong to the kernel process, kernel and administrator only).
        - `type`: task type. Permissions: anyone (except tasks that belong to the kernel process, kernel and administrator only).
        - `status`: current task status. Permissions: anyone (except tasks that belong to the kernel process, kernel and administrator only).
        - `priority`: scheduling priority. Permissions: anyone (except tasks that belong to the kernel process, kernel and administrator only).
        - `flags`: task flags bitfield. Permissions: anyone (except tasks that belong to the kernel process, kernel and administrator only).
        - `exit_code`: termination status. Permissions: anyone (except tasks that belong to the kernel process, kernel and administrator only).
        - `function`: task entry function pointer. Permissions: kernel, administrator, and owner process.
        - `parameter`: task entry parameter pointer. Permissions: kernel, administrator, and owner process.
        - `architecture`: architecture-specific context view. Permissions: kernel and administrator only.
          - `context`: raw context pointer. Permissions: kernel and administrator only.
          - `stack`: architecture stack view. Permissions: kernel and administrator only.
          - `system_stack`: architecture system stack view. Permissions: kernel and administrator only.
        - `stack`: task stack view. Permissions: kernel and administrator only.
          - `base`: stack base address. Permissions: kernel and administrator only.
          - `size`: stack size. Permissions: kernel and administrator only.
        - `system_stack`: system stack pointer. Permissions: kernel and administrator only.
        - `wake_up_time`: scheduler wake-up time. Permissions: kernel and administrator only.
        - `mutex`: mutex pointer. Permissions: kernel and administrator only.
        - `message_queue`: message queue pointer. Permissions: kernel and administrator only.
        - `process`: owning process pointer. Permissions: kernel and administrator only.
- `drivers`: Kernel driver list root. provides indexed access to driver views. Permissions: kernel and administrator only.
  - `drivers.count`: number of drivers. Permissions: kernel and administrator only.
  - `drivers[n]`: driver view at index `n`. Permissions: kernel and administrator only.
    - `type`: driver type. Permissions: kernel and administrator only.
    - `version_major`: major version. Permissions: kernel and administrator only.
    - `version_minor`: minor version. Permissions: kernel and administrator only.
    - `designer`: driver designer. Permissions: kernel and administrator only.
    - `manufacturer`: driver manufacturer. Permissions: kernel and administrator only.
    - `product`: driver product name. Permissions: kernel and administrator only.
    - `flags`: driver flags. Permissions: kernel and administrator only.
    - `command`: driver command pointer. Permissions: kernel and administrator only.
    - `enum_domain_count`: number of enum domains. Permissions: kernel and administrator only.
    - `enum_domains`: enum domain array. Permissions: kernel and administrator only.
      - `enum_domains.count`: enum domain count. Permissions: kernel and administrator only.
      - `enum_domains[n]`: enum domain value at index `n`. Permissions: kernel and administrator only.
- `storage`: Storage list root. provides indexed access to storage views. Permissions: anyone.
  - `storage.count`: number of storage objects. Permissions: anyone.
  - `storage[n]`: storage view at index `n`. Permissions: anyone.
    - `type`: storage type. Permissions: anyone.
    - `removable`: removable flag. Permissions: anyone.
    - `bytes_per_sector`: sector size in bytes. Permissions: anyone.
    - `num_sectors_low`: lower 32-bit sector count. Permissions: anyone.
    - `num_sectors_high`: upper 32-bit sector count. Permissions: anyone.
    - `access`: access mode flags. Permissions: anyone.
    - `driver_manufacturer`: backing driver manufacturer. Permissions: anyone.
    - `driver_product`: backing driver product name. Permissions: anyone.
- `pci_bus`: PCI bus list root. provides indexed access to PCI bus views. Permissions: anyone.
  - `pci_bus.count`: number of PCI buses. Permissions: anyone.
  - `pci_bus[n]`: PCI bus view at index `n`. Permissions: anyone.
    - `number`: PCI bus number. Permissions: anyone.
    - `device_count`: number of PCI devices on the bus. Permissions: anyone.
- `pci_device`: PCI device list root. provides indexed access to PCI device views. Permissions: anyone.
  - `pci_device.count`: number of PCI devices. Permissions: anyone.
  - `pci_device[n]`: PCI device view at index `n`. Permissions: anyone.
    - `bus`: PCI bus number. Permissions: anyone.
    - `device`: PCI device number. Permissions: anyone.
    - `function`: PCI function number. Permissions: anyone.
    - `vendor_id`: PCI vendor identifier. Permissions: anyone.
    - `device_id`: PCI device identifier. Permissions: anyone.
    - `base_class`: PCI base class code. Permissions: anyone.
    - `sub_class`: PCI subclass code. Permissions: anyone.
    - `prog_if`: PCI programming interface code. Permissions: anyone.
    - `revision`: PCI revision code. Permissions: anyone.
- `keyboard`: Keyboard exposure root. provides access to keyboard state. Permissions: anyone.
  - `keyboard.layout`: active keyboard layout code. Permissions: anyone.
  - `keyboard.driver`: active keyboard driver. Permissions: kernel and administrator only.
- `mouse`: Mouse exposure root. provides access to mouse state. Permissions: anyone.
  - `mouse.x`: cursor X coordinate. Permissions: anyone.
  - `mouse.y`: cursor Y coordinate. Permissions: anyone.
  - `mouse.driver`: active mouse driver. Permissions: kernel and administrator only.
- `usb.ports`: xHCI port list root. provides indexed access to USB ports. Permissions: kernel and administrator only.
  - `usb.ports.count`: number of USB ports. Permissions: kernel and administrator only.
  - `usb.ports[n]`: port view at index `n`. Permissions: kernel and administrator only.
    - `bus`: PCI bus number. Permissions: kernel and administrator only.
    - `device`: PCI device number. Permissions: kernel and administrator only.
    - `function`: PCI function number. Permissions: kernel and administrator only.
    - `port_number`: xHCI port index. Permissions: kernel and administrator only.
    - `port_status`: raw port status. Permissions: kernel and administrator only.
    - `speed_id`: link speed identifier. Permissions: kernel and administrator only.
    - `connected`: connection status. Permissions: kernel and administrator only.
    - `enabled`: port enable state. Permissions: kernel and administrator only.
- `usb.devices`: USB device list root. provides indexed access to USB devices. Permissions: anyone.
  - `usb.devices.count`: number of USB devices. Permissions: anyone.
  - `usb.devices[n]`: device view at index `n`. Permissions: anyone.
    - `bus`: PCI bus number. Permissions: anyone.
    - `device`: PCI device number. Permissions: anyone.
    - `function`: PCI function number. Permissions: anyone.
    - `port_number`: xHCI port index. Permissions: anyone.
    - `address`: USB device address. Permissions: anyone.
    - `speed_id`: link speed identifier. Permissions: anyone.
    - `vendor_id`: USB vendor ID. Permissions: anyone.
    - `product_id`: USB product ID. Permissions: anyone.


## Hardware and Driver Stack

### Driver architecture

Hardware-facing components are grouped under `kernel/source/drivers` with public headers in `kernel/include/drivers`. This area contains keyboard, serial mouse, interrupt controller (I/O APIC), PCI bus, network (`E1000`), storage (`ATA`, `SATA`, `NVMe`), graphics (`VGA`, `VESA`, mode tables), and file system backends (`FAT16`, `FAT32`, `EXFS`).

Kernel-side registration follows a deterministic list-driven flow in `KernelData.c`: `InitializeDriverList()` appends static driver descriptors, then `LoadAllDrivers()` walks the list in order.

The NVMe driver initializes admin queues first, then I/O queues, configures completion interrupts through MSI-X when available, enumerates namespaces, and registers each namespace as a disk so `MountDiskPartitions` can attach file systems.


### Input device stack

Mouse input is centralized in `kernel/source/MouseCommon.c`, which buffers deltas/buttons, dispatches events, and selects the active mouse driver. USB HID mouse support (`kernel/source/drivers/Mouse-USB.c`) takes priority over the serial mouse when a compatible USB device is present.

Keyboard selection is handled by the keyboard selector driver, keeping one active keyboard path at a time while sharing the same higher-level input/message routing model.


### USB host and class stack

USB foundations are defined in `kernel/include/drivers/USB.h`, including shared type definitions (speed tiers, endpoint kinds, addressing) and standard descriptor layouts used by host controller and class drivers.

The xHCI host stack (`kernel/source/drivers/XHCI-Core.c`, `kernel/source/drivers/XHCI-Device-Lifecycle.c`, `kernel/source/drivers/XHCI-Device-Transfer.c`, `kernel/source/drivers/XHCI-Device-Enum.c`, `kernel/source/drivers/XHCI-Hub.c`, `kernel/source/drivers/XHCI-Enum.c`) is attached by the PCI subsystem and performs:

- controller halt/reset/run sequencing,
- MMIO mapping and ring allocation (DCBAA, command ring, event ring),
- interrupter programming,
- EP0 control transfers for enumeration,
- topology construction (device/config/interface/endpoint),
- operational reporting via `usbctl ports`, `usbctl probe`, `usbctl devices`.

USB interfaces and endpoints are kernel objects stored in global lists. Class drivers hold references so teardown is deferred until hotplug release is safe.

Disconnect handling is staged: stop and reset endpoints, flush transfer rings, disable slot context, then release resources only after object references drain. This avoids invalid memory access during in-flight I/O.

Hub-class devices are supported through descriptor parsing, port power management, downstream tracking, and interrupt-endpoint polling for port-change driven reset/re-enumeration.

USB mass storage (`kernel/source/drivers/USBMassStorage.c`, BOT read-only path) configures bulk endpoints, executes CBW/CSW transactions for SCSI `INQUIRY`, `READ CAPACITY(10)`, and `READ(10)`, then registers discovered media in the global disk list for `MountDiskPartitions`. Active USB storage is tracked in `Kernel.USBDevice`; shell command `usb drives` reports address, VID/PID, and block geometry. When SystemFS is ready, new partitions are attached under `/fs/<volume>`. On removal, associated file systems are detached and released. Mount/unmount events broadcast `ETM_USB_MASS_STORAGE_MOUNTED` and `ETM_USB_MASS_STORAGE_UNMOUNTED` to userland process message queues.


### Graphics and console paths

The VESA driver requests VBE modes in linear frame buffer mode (INT 10h 4F02h, bit 14), validates LFB capability, and maps `PhysBasePtr` through `MapIOMemory`. Rendering writes directly to mapped VRAM, avoiding BIOS bank-switch calls.

`kernel/include/GFX.h` defines a backend-facing graphics command contract. Legacy drawing commands (`SETMODE`, `GETMODEINFO`, `SETPIXEL`, `GETPIXEL`, `LINE`, `RECTANGLE`) stay unchanged for compatibility. The same header also defines optional backend commands for capabilities/outputs/present/surfaces (`GETCAPABILITIES`, `ENUMOUTPUTS`, `GETOUTPUTINFO`, `PRESENT`, `WAITVBLANK`, `ALLOCSURFACE`, `FREESURFACE`, `SETSCANOUT`). Legacy backends that do not implement those optional commands return `DF_RETURN_NOT_IMPLEMENTED`.

The console supports direct linear framebuffer rendering when Multiboot framebuffer metadata is available:

- BIOS/MBR path uses VGA text buffer `0xB8000` with text framebuffer metadata.
- UEFI path uses GOP-provided framebuffer base, pitch, resolution, and RGB layout, with glyph rendering into GOP memory.
- In framebuffer mode, a software cursor is rendered by inverting the active text cell and restored on each cursor move, so command-line editing keeps a visible caret position.

The default font is an in-tree ASCII 8x16 EXOS font and can be replaced through the font API.


### Early boot console path

`kernel/source/EarlyBootConsole.c` provides a minimal framebuffer text path independent from normal console initialization. It writes glyphs through physical framebuffer mappings and is used for early boot and memory-initialization checkpoints.


### ACPI services

Advanced power management and reset paths live in `kernel/source/ACPI.c`. The module discovers ACPI tables, exposes the parsed configuration, and offers helpers for platform control. `ACPIShutdown()` releases ACPI mappings and state without powering off. `ACPIPowerOff()` enters the S5 soft-off state using the `_S5` sleep type from the DSDT when available (defaults to 7 otherwise) and falls back to legacy power-off sequences when the ACPI path fails. The new `ACPIReboot()` companion performs a warm reboot by first using the ACPI reset register (when present) and then chaining to legacy reset controllers to ensure the machine restarts even on older chipsets. Kernel-level wrappers `ShutdownKernel()` and `RebootKernel()` drive shell commands, clear userland processes, then kernel tasks, and perform a reverse-order driver unload before handing control to the ACPI routines so subsystems leave as few pending resources as possible when the machine powers off or reboots.


### Disk interfaces

```
+------------------------------------+
|          Operating System          |
+------------------------------------+
                |
                | (Software Drivers)
                v
+------------------------------------+
| Controllers/Protocols              |
| +--------+  +--------+  +--------+ |
| |  AHCI  |  |  NVMe  |  |  SCSI  | |
| +--------+  +--------+  +--------+ |
|      |           |           |     |
|      v           v           v     |
| +--------+  +--------+  +--------+ |
| |  SATA  |  |  PCIe  |  |  SAS/  | |
| |Interface| |Interface| |SATA Int| |
| +--------+  +--------+  +--------+ |
|      |           |           |     |
|      v           v           v     |
| +--------+  +--------+  +--------+ |
| | HDD,   |  | SSD    |  | HDD,   | |
| | SSD    |  | NVMe   |  | SSD    | |
| | (SATA) |  |        |  | (SAS/  | |
| |        |  |        |  | SATA)  | |
| +--------+  +--------+  +--------+ |
+------------------------------------+
```

**AHCI interrupt policy**: the SATA driver registers the controller with the shared `DeviceInterruptRegister` infrastructure and installs dedicated top and bottom halves so IRQ 11 traffic can be routed through a private slot when the hardware gets its own vector (MSI/MSI-X or a non-shared INTx line). Commands still complete synchronously, therefore all AHCI per-port interrupt masks (`PORT.ie`) and the global `GHC.IE` bit stay cleared in shipping builds to keep the shared IRQ 11 line quiet for the `E1000` NIC.
Disk drivers expose `BytesPerSector` through `DF_DISK_GETINFO` (`DISKINFO.BytesPerSector`). Partition probing in `FileSystem.c` consumes this value and accepts 512-byte and 4096-byte sectors when reading MBR/GPT and signature data.


## Storage and Filesystems

### File systems

#### Global state and object model

File system state is split between:
- `Kernel.FileSystem`: mounted `FILESYSTEM` objects,
- `Kernel.UnusedFileSystem`: discovered but non-mounted `FILESYSTEM` objects,
- `Kernel.FileSystemInfo`: global metadata (`ActivePartitionName`),
- `Kernel.SystemFS`: virtual filesystem wrapper (`SYSTEMFSFILESYSTEM`) used as the global path entry point.

Each `FILESYSTEM` object carries runtime fields (`Driver`, `StorageUnit`, `Mounted`, `Mutex`, `Name`) plus partition metadata in `PARTITION` (`Scheme`, `Type`, `Format`, `Index`, `Flags`, `StartSector`, `NumSectors`, `TypeGuid`).

`FileSystemGetStorageUnit()` and `FileSystemHasStorageUnit()` expose backing storage uniformly for disk-backed and virtual filesystems. Display helpers (`FileSystemGetPartitionSchemeName`, `FileSystemGetPartitionTypeName`, `FileSystemGetPartitionFormatName`) centralize partition labeling.

#### Discovery and mount pipeline

`InitializeFileSystems()` is the main orchestration path:
1. Clear `ActivePartitionName`.
2. Release stale entries from `Kernel.UnusedFileSystem`.
3. Scan `Kernel.Disk` and call `MountDiskPartitions()` for each storage unit.
4. Select an active partition by searching for `exos.toml`/`EXOS.TOML` (`FileSystemSelectActivePartitionFromConfig()`).
5. Build and mount SystemFS (`MountSystemFS()`).
6. Load kernel configuration (`ReadKernelConfiguration()`).
7. Apply configured user mounts (`MountUserNodes()` via `SystemFS.Mount.<index>.*` keys).
8. Resolve logical kernel paths through `KernelPath.<name>` keys when subsystems request configured file or folder locations.

Logical kernel path keys are consumed through `utils/KernelPath`:
- `KernelPath.UsersDatabase`: absolute VFS file path used by user account persistence.
- `KernelPath.KeyboardLayouts`: absolute VFS folder path used to load keyboard layout files (`<layout>.ekm1`).
- `KernelPath.SystemAppsRoot`: absolute VFS folder path used by shell package-name resolution (`package run <name>`).

`MountDiskPartitions()` handles MBR and switches to GPT parsing when a protective MBR entry (`0xEE`) is detected. Supported formats are mounted through dedicated drivers (FAT16/FAT32/NTFS/EXFS/EXT2 path); partition metadata is written with `SetFileSystemPartitionInfo()`. Non-mounted partitions are still materialized through `RegisterUnusedFileSystem()` so diagnostics and shell tooling can inspect them.

When SystemFS is ready (`FileSystemReady()`), newly mounted filesystems are attached into SystemFS under `/fs/<volume>` through `SystemFSMountFileSystem()`.

#### Mounted volume naming

Mounted partition names are generated by `GetDefaultFileSystemName()` (`kernel/source/FileSystem.c`) and exposed under `/fs/<volume>`.

Format:
- `<prefix><disk_index>p<partition_index>`

Prefix by storage driver type:
- `r` for RAM disks (`DRIVER_TYPE_RAMDISK`)
- `f` for floppy disks (`DRIVER_TYPE_FLOPPYDISK`)
- `u` for USB mass storage (`DRIVER_TYPE_USB_STORAGE`)
- `n` for NVMe storage (`DRIVER_TYPE_NVME_STORAGE`)
- `s` for SATA/AHCI storage (`DRIVER_TYPE_SATA_STORAGE`)
- `a` for ATA storage (`DRIVER_TYPE_ATA_STORAGE`)
- `d` for all other disk drivers (fallback)

Index rules:
- `disk_index` is zero-based and counted among disks of the same driver type.
- `partition_index` is zero-based and comes from the partition enumeration path (MBR slot index or GPT entry index).

Examples:
- `/fs/s0p0`
- `/fs/n1p0`
- `/fs/u0p0`
- `/fs/f0p0`
- `/fs/r0p0`

#### EPK package format

The EPK package binary layout is frozen for parser/tooling integration in:
- `documentation/EPKBinaryFormat.md`
- `kernel/include/package/EpkFormat.h`
- `kernel/include/package/EpkParser.h`
- `kernel/source/package/EpkParser.c`

The format is a strict on-disk contract:
- fixed 128-byte header (`EPK_HEADER`) with explicit section offsets/sizes and package hash,
- TOC section (`EPK_TOC_HEADER` + `EPK_TOC_ENTRY` records + variable UTF-8 path blobs),
- block table section (`EPK_BLOCK_ENTRY` records for compressed chunks),
- dedicated manifest blob (`manifest.toml`) and optional detached signature blob.

Compatibility is fail-closed by contract:
- unknown flags or unsupported version are rejected,
- reserved fields must stay zero,
- malformed section bounds/order are rejected with deterministic validation status codes.

Step-3 parser/validator behavior:
- validates header layout and section bounds/order before deeper parsing,
- parses TOC entries and block table into kernel-side parsed descriptors,
- validates package hash (`SHA-256`) over package bytes excluding signature region,
- optionally validates detached signature blob through `utils/Signature`,
- returns stable validation status codes and logs explicit parse failures with function-tagged error messages.

#### PackageFS readonly mount

Step-4 introduces a dedicated PackageFS module:
- `kernel/include/package/PackageFS.h`
- `kernel/source/package/PackageFS.c`
- `kernel/source/package/PackageFS-Tree.c`
- `kernel/source/package/PackageFS-File.c`
- `kernel/source/package/PackageFS-Mount.c`

PackageFS mounts one validated `.epk` archive as a virtual read-only filesystem:
- mount entry point: `PackageFSMountFromBuffer(...)`,
- unmount entry point: `PackageFSUnmount(...)`,
- TOC tree materialization for files, folders, and folder aliases,
- wildcard folder enumeration through `DF_FS_OPENFILE` + `DF_FS_OPENNEXT`,
- write-class operations (`create`, `delete`, `rename`, `write`, `set volume info`) rejected with `DF_RETURN_NO_PERMISSION`,
- unmount refused when open handles still reference the mounted package,
- block-backed file reads mapped to table ranges with on-demand per-chunk decompression,
- per-chunk SHA-256 validation against block table hashes before serving data,
- bounded decompressed chunk caching through `utils/ChunkCache`, with cleanup-based eviction and full invalidation during unmount.

#### Package namespace integration

Step-6 namespace integration is implemented by:
- `kernel/include/package/PackageNamespace.h`
- `kernel/source/package/PackageNamespace.c`

Package integration targets per-process launch behavior:
- packaged application receives a private `/package` mount,
- packaged application receives `/user-data` alias to `/users/<current-user>/<package-name>/data`,
- no global application package mount graph is required for launch.

Process-view hooks:
- `PackageNamespaceBindCurrentProcessPackageView(...)` mounts one package view at `/package`.
- the same helper maps `/user-data` to `/users/<current-user>/<package-name>/data` on the active filesystem.
- package namespace roots and aliases are resolved through `utils/KernelPath` keys:
  - `KernelPath.UsersRoot`
  - `KernelPath.CurrentUserAlias`
  - `KernelPath.PrivatePackageAlias`
  - `KernelPath.PrivateUserDataAlias`

#### Package manifest compatibility checks

Step-7 manifest resolution is implemented by:
- `kernel/include/package/PackageManifest.h`
- `kernel/source/package/PackageManifest.c`

Launch validation flow includes:
- parse and validate embedded `manifest.toml` (identity + compatibility fields),
- enforce architecture and kernel compatibility policy before mount activation,
- reject incompatible packages with deterministic diagnostics.

Manifest model is strict and dependency-free:
- required keys: `name`, `version`, `arch`, `kernel_api`, `entry`,
- optional table: `[commands]` (`command-name -> package-relative executable path`),
- accepted architecture values: `x86-32`, `x86-64`,
- `kernel_api` compatibility policy: `required.major == kernel.major` and `required.minor <= kernel.minor`,
- `provides` and `requires` keys are rejected.

No dependency solver behavior is part of this model:
- no provider graph,
- no transitive dependency resolution,
- no global package activation transaction state for application launches.

Launch target rules:
- `entry` is the default launch target for the package.
- `commands.<name>` exposes additional named launch targets for multi-binary packages.
- command-name collisions do not use implicit priority; ambiguous matches fail with explicit diagnostics.

Command resolution without package name is deterministic:
1. path token (contains `/`) runs as direct path,
2. user alias namespace (`/users/<user>/commands/<name>`),
3. system alias namespace (`/system/commands/<name>`),
4. package command index (`commands.<name>` across known packages),
5. on multiple package matches, launch is rejected as ambiguous.

#### Package launch flow

Step-8 launch activation is wired in shell launch path (`SpawnExecutable`):
- when target extension is `.epk`, shell reads package bytes from disk,
- package manifest is parsed and compatibility-checked before activation,
- package is mounted through `PackageFSMountFromBuffer(...)`,
- package aliases are bound through `PackageNamespaceBindCurrentProcessPackageView(...)`,
- manifest `entry` is executed from `/package/...`,
- launch failures trigger explicit unbind/unmount cleanup with no partial mounted leftovers,
- background launches keep mounted package filesystem attached to process state and release it during process teardown.

Shell package command:
- `package run <package-name> [command-name] [args...]` resolves package file from `KernelPath.SystemAppsRoot`,
- if `command-name` matches `manifest.commands.<name>`, that target is launched,
- otherwise launch falls back to `manifest.entry` and keeps the token as the first application argument.
- `package list <package-name|path.epk>` validates/mounts one package and lists manifest metadata plus package tree content.
- `package add <package-name|path.epk>` validates source package and copies it to `KernelPath.SystemAppsRoot` under `<manifest.name>.epk`.

#### Runtime access paths

`OpenFile()` takes two routing paths:
- absolute path (`/...`): delegated to SystemFS (`DF_FS_OPENFILE`), which can traverse mounted nodes and forward to backing filesystems;
- non-absolute path: probes mounted filesystems in `Kernel.FileSystem` until one resolves the file.

The file layer is synchronized with `MUTEX_FILESYSTEM`, and open handles are tracked in `Kernel.File` with per-file ownership and reference management (`OwnerTask`, `OpenFlags`, refcount).

#### Removable storage behavior

USB mass storage hot-plug integrates with the same pipeline:
- on attach, `USBMassStorageStartDevice()` calls `MountDiskPartitions()` only when `FileSystemReady()` is true;
- on detach, `USBMassStorageDetachFileSystems()` unmounts from SystemFS (`SystemFSUnmountFileSystem()`), releases mounted and unused filesystem objects for that disk, and clears `ActivePartitionName` when the removed volume was active.

Mount and unmount notifications are broadcast to processes (`ETM_USB_MASS_STORAGE_MOUNTED` / `ETM_USB_MASS_STORAGE_UNMOUNTED`).


### EXOS File System - EXFS

#### Structure of the Master Boot Record

| Offset   | Type | Description                                  |
|----------|------|----------------------------------------------|
| 0..445   | U8x? | The boot sequence                            |
| 446..461 | ?    | CHS location of partition No 1               |
| 462..477 | ?    | CHS location of partition No 2               |
| 478..493 | ?    | CHS location of partition No 3               |
| 494..509 | ?    | CHS location of partition No 4               |
| 510      | U16  | BIOS signature : 0x55AA (`_*_*_*_**_*_*_*_`) |

---

#### Structure of SuperBlock

The SuperBlock is always **1024 bytes** in size.

| Offset | Type   | Description                                   |
|--------|--------|-----------------------------------------------|
| 0      | U32    | Magic number, must be `"EXOS"`                |
| 4      | U32    | Version (high word = major, low word = minor) |
| 8      | U32    | Size of a cluster in bytes                    |
| 12     | U32    | Number of clusters                            |
| 16     | U32    | Number of free clusters                       |
| 20     | U32    | Cluster index of cluster bitmap               |
| 24     | U32    | Cluster index of bad cluster page             |
| 28     | U32    | Cluster index of root FileRecord ("/")        |
| 32     | U32    | Cluster index of security info                |
| 36     | U32    | Index in root for OS kernel main file         |
| 40     | U32    | Number of folders (excluding "." and "..")    |
| 44     | U32    | Number of files                               |
| 48     | U32    | Max mount count before check is forced        |
| 52     | U32    | Current mount count                           |
| 56     | U32    | Format of the volume name                     |
| 60–63  | U8x4   | Reserved                                      |
| 64     | U8x32  | Password (optional)                           |
| 96     | U8x32  | Name of this file system's creator            |
| 128    | U8x128 | Name of the volume                            |

---

#### Structure of FileRecord

| Offset | Type   | Description                        |
|--------|--------|------------------------------------|
| 0      | U32    | SizeLow                            |
| 4      | U32    | SizeHigh                           |
| 8      | U64    | Creation time                      |
| 16     | U64    | Last access time                   |
| 24     | U64    | Last modification time             |
| 32     | U32    | Cluster index for ClusterTable     |
| 36     | U32    | Standard attributes                |
| 40     | U32    | Security attributes                |
| 44     | U32    | Group owner of this file           |
| 48     | U32    | User owner of this file            |
| 52     | U32    | Format of name                     |
| 56–127 | U8x?   | Reserved, should be zero           |
| 128    | U8x128 | Name of the file (NULL terminated) |

---

#### FileRecord fields

**Time fields (bit layout):**

- Bits 0..21  : Year (max: 4,194,303)
- Bits 22..25 : Month in the year (max: 15)
- Bits 26..31 : Day in the month (max: 63)
- Bits 32..37 : Hour in the day (max: 63)
- Bits 38..43 : Minute in the hour (max: 63)
- Bits 44..49 : Second in the minute (max: 63)
- Bits 50..59 : Millisecond in the second (max: 1023)

**Standard attributes field:**

- Bit 0 : 1 = folder, 0 = file
- Bit 1 : 1 = read-only, 0 = read/write
- Bit 2 : 1 = system
- Bit 3 : 1 = archive
- Bit 4 : 1 = hidden

**Security attributes field:**

- Bit 0 : 1 = only kernel has access to the file
- Bit 1 : 1 = fill the file's clusters with zeroes on delete

**Name format:**

- 0 : ASCII (8 bits per character)
- 1 : Unicode (16 bits per character)

---

#### Structure of folders and files

- A cluster that contains 32-bit indices to other clusters is called a **page**.
- FileRecord contains a cluster index for its first page.
- A page is filled with cluster indices pointing to file/folder data.
- For folders: data = series of FileRecords.
- For files: data = arbitrary user data.
- The **last entry** of a page is `0xFFFFFFFF`.
- If more than one page is needed, the last index points to the **next page**.

---

#### Clusters

- All cluster pointers are 32-bit.
- Cluster 0 = boot sector (1024 bytes).
- Cluster 1 = SuperBlock (1024 bytes).
- First usable cluster starts at byte 2048.

**Max addressable bytes by cluster size:**

| Cluster size | Max addressable bytes  |
|--------------|-------------------------|
| 1024         | 4,398,046,510,080       |
| 2048         | 8,796,093,020,160       |
| 4096         | 17,592,186,040,320      |
| 8192         | 35,184,372,080,640      |

**Number of clusters formula:**

```
(Disc size in bytes - 2048) / Cluster size
```

Fractional part = unusable space.

**Examples:**

| Disc size               | Cluster size | Total clusters |
|-------------------------|--------------|----------------|
| 536,870,912 (500 MB)    | 1,024 (1 KB) | 524,286        |
| 536,870,912 (500 MB)    | 2,048 (2 KB) | 262,143        |
| 536,870,912 (500 MB)    | 4,096 (4 KB) | 131,071        |
| 536,870,912 (500 MB)    | 8,192 (8 KB) | 65,535         |
| 4,294,967,296 (4 GB)    | 1,024 (1 KB) | 4,194,302      |
| 4,294,967,296 (4 GB)    | 2,048 (2 KB) | 2,097,151      |
| 4,294,967,296 (4 GB)    | 4,096 (4 KB) | 1,048,575      |
| 4,294,967,296 (4 GB)    | 8,192 (8 KB) | 524,287        |

---

#### Cluster bitmap

- A bit array showing free/used clusters.
- `0 = free`, `1 = used`.
- Size of bitmap =

```
(Total disc size / Cluster size) / 8
```

**Examples:**

| Disc size              | Cluster size | Bitmap size | Num. clusters |
|------------------------|--------------|-------------|---------------|
| 536,870,912 (500 MB)   | 1,024 (1 KB) | 65,536      | 64            |
| 536,870,912 (500 MB)   | 2,048 (2 KB) | 32,768      | 16            |
| 536,870,912 (500 MB)   | 4,096 (4 KB) | 16,384      | 4             |
| 536,870,912 (500 MB)   | 8,192 (8 KB) | 8,192       | 1             |
| 4,294,967,296 (4 GB)   | 1,024 (1 KB) | 524,288     | 512           |
| 4,294,967,296 (4 GB)   | 2,048 (2 KB) | 262,144     | 128           |
| 4,294,967,296 (4 GB)   | 4,096 (4 KB) | 131,072     | 32            |
| 4,294,967,296 (4 GB)   | 8,192 (8 KB) | 65,536      | 8             |
| 17,179,869,184 (16 GB) | 1,024 (1 KB) | 4,194,304   | 8,192         |
| 17,179,869,184 (16 GB) | 2,048 (2 KB) | 1,048,576   | 512           |
| 17,179,869,184 (16 GB) | 4,096 (4 KB) | 524,288     | 128           |
| 17,179,869,184 (16 GB) | 8,192 (4 KB) | 262,144     | 32            |


### Filesystem Cluster cache

The shared cluster cache helper is implemented in `kernel/source/drivers/filesystems/ClusterCache.c` with its public interface in `kernel/include/drivers/filesystems/ClusterCache.h`. It reuses the generic `utils/Cache` engine (TTL, cleanup, eviction) and adds cluster-oriented keys (`owner + cluster index + size`) so multiple filesystem drivers can share one non-duplicated cache pattern. The generic cache supports `CACHE_WRITE_POLICY_READ_ONLY`, `CACHE_WRITE_POLICY_WRITE_THROUGH`, and `CACHE_WRITE_POLICY_WRITE_BACK`, with optional flush callbacks for dirty entry persistence.


### Foreign File systems

| FS | Key Concepts | RO Difficulty | Full RW Difficulty | Notes |
|---|---|---:|---:|---|
| **FAT12/16** | Boot-friendly, allocation tables, 8.3 names | 2 | 3 | Very simple; some edge cases with cluster chains. |
| **ISO9660/Joliet/Rock Ridge** | CD-ROM FS, fixed tables | 2 | 2 | Read-only only; trivial for mounting images. |
| **MINIX (v1/v2)** | Bitmaps, inodes, direct/indirect | 3 | 4 | Educational, limited size, very clean spec. |
| **FAT32** | FAT + FSInfo + VFAT long names | 3 | 4 | Long File Names, timestamp quirks, no journal. |
| **squashfs** | Read-only, compressed, indexed tables | 3 | 3 | Dead simple in RO; great for system images. |
| **exFAT** | Bitmap + FAT, chained dir entries | 4 | 6 | Official specs exist, but many entry types. |
| **UDF** | Successor to ISO9660, incremental writes | 4 | 6–7 | Many versions/profiles; optical and USB use. |
| **ext2** | Superblock, group desc, bitmaps, inodes | 5 | 6 | Very documented; no journal; fsck required. |
| **ext3** | ext2 + JBD journal | 6 | 7 | Journaling metadata/data, proper recovery required. |
| **ReiserFS (v3)** | Balanced trees, small entry packing | 6 | 8 | Non-standard layout; legacy. |
| **HFS+** | B-trees (catalog, extents), forks | 6 | 8 | Unicode normalization, legacy quirks. |
| **ext4** | Extents, htree, 64-bit, JBD2 | 6 | 9 | Extents + journal + optional features. |
| **XFS** | Btrees everywhere, delayed alloc, journaling | 6 | 9 | High-performance, recovery heavy. |
| **F2FS** | Log-structured, flash segments | 6 | 8 | GC/segment cleaning, wear-level tuning. |
| **APFS** | Copy-on-write, containers, snapshots | 7 | 9–10 | Encryption, clones, variable blocks; partial docs. |
| **Btrfs** | COW, extent trees, checksums, RAID, snapshots | 7 | 9–10 | Complex balance between many trees; fragile. |
| **ZFS** | COW, pools, checksums, RAID-Z, snapshots | 7 | 10 | Includes volume mgmt; very large scope. |
| **NTFS** | MFT, resident/non-resident attrs, bitmap, journal | 7 | 9 | Compression, sparse, ACLs, USN; very rich design. |

#### EXT2

The EXT2 driver implementation is split into focused units under
`kernel/source/drivers/filesystems/`:
`EXT2-Base.c`, `EXT2-Allocation.c`, `EXT2-Storage.c`, and
`EXT2-FileOps.c`.

```
                ┌──────────────────────────────────────┐
                │               INODE                  │
                ├──────────────────────────────────────┤
                │ Block[0] → [DATA BLOCK 0]            │
                │ Block[1] → [DATA BLOCK 1]            │
                │   ...                                │
                │ Block[11] → [DATA BLOCK 11]          │
                │ Block[12] → [SINGLE INDIRECT]        │
                │ Block[13] → [DOUBLE INDIRECT]        │
                │ Block[14] → [TRIPLE INDIRECT]        │
                └──────────────────────────────────────┘
                               │
                               │
                               ▼
─────────────────────────────────────────────────────────────────────
(1) SINGLE INDIRECT (Block[12])
─────────────────────────────────────────────────────────────────────
[SINGLE INDIRECT BLOCK]
 ├── ptr[0] → [DATA BLOCK 12]
 ├── ptr[1] → [DATA BLOCK 13]
 ├── ptr[2] → [DATA BLOCK 14]
 ...
 └── ptr[1023] → [DATA BLOCK N]

─────────────────────────────────────────────────────────────────────
(2) DOUBLE INDIRECT (Block[13])
─────────────────────────────────────────────────────────────────────
[DOUBLE INDIRECT BLOCK]
 ├── ptr[0] → [SINGLE INDIRECT BLOCK A]
 │              ├── ptr[0] → [DATA BLOCK A0]
 │              ├── ptr[1] → [DATA BLOCK A1]
 │              └── ...
 ├── ptr[1] → [SINGLE INDIRECT BLOCK B]
 │              ├── ptr[0] → [DATA BLOCK B0]
 │              ├── ptr[1] → [DATA BLOCK B1]
 │              └── ...
 └── ptr[1023] → [SINGLE INDIRECT BLOCK Z]
                 ├── ...
                 └── [DATA BLOCK Zx]

─────────────────────────────────────────────────────────────────────
(3) TRIPLE INDIRECT (Block[14])
─────────────────────────────────────────────────────────────────────
[TRIPLE INDIRECT BLOCK]
 ├── ptr[0] → [DOUBLE INDIRECT BLOCK A]
 │              ├── ptr[0] → [SINGLE INDIRECT BLOCK A1]
 │              │              ├── ptr[0] → [DATA BLOCK A1-0]
 │              │              └── ...
 │              ├── ptr[1] → [SINGLE INDIRECT BLOCK A2]
 │              │              ├── ptr[0] → [DATA BLOCK A2-0]
 │              │              └── ...
 │              └── ...
 ├── ptr[1] → [DOUBLE INDIRECT BLOCK B]
 │              └── ...
 └── ...
─────────────────────────────────────────────────────────────────────
```

#### NTFS

```
                  ┌─────────────────────────────────────────┐
                  │           NTFS BOOT SECTOR              │
                  ├─────────────────────────────────────────┤
                  │ BPB/EBPB                                │
                  │ BytesPerSector, SectorsPerCluster       │
                  │ MFT LCN, MFTMirr LCN, Record size       │
                  └─────────────────────────────────────────┘
                                   │
               ┌───────────────────┴───────────────────┐
               ▼                                       ▼
┌──────────────────────────────────┐      ┌───────────────────────────┐
│ $MFT (Master File Table)         │      │ $MFTMirr                  │
│ record 0..N                      │      │ mirror of first records   │
└──────────────────────────────────┘      └───────────────────────────┘
               │
               ▼
┌─────────────────────────────────────────────────────────────────────┐
│ FILE RECORD (typically 1024 bytes)                                  │
├─────────────────────────────────────────────────────────────────────┤
│ Header: "FILE", USA/Fixup array, sequence, flags, attr offsets      │
│ Attributes (typed TLV chain):                                       │
│ - STANDARD_INFORMATION                                              │
│ - FILE_NAME (parent ref + UTF-16 name)                              │
│ - DATA (resident or non-resident)                                   │
│ - INDEX_ROOT / INDEX_ALLOCATION / BITMAP (folders)                  │
│ - other metadata attributes                                         │
└─────────────────────────────────────────────────────────────────────┘
               │
   ┌───────────┴───────────┐
   ▼                       ▼
┌───────────────────┐   ┌────────────────────────────────────────────┐
│ Resident DATA     │   │ Non-resident DATA                          │
│ bytes in record   │   │ Runlist: (VCN range -> LCN range)          │
└───────────────────┘   │ sparse/compressed flags possible           │
                        └────────────────────────────────────────────┘

───────────────────────────────────────────────────────────────────────
Folder indexing (B+tree-like)
───────────────────────────────────────────────────────────────────────
INDEX_ROOT (small entries in record)
    └─ if overflow -> INDEX_ALLOCATION blocks + BITMAP allocation map
         each index entry: filename key + file reference (MFT index)

───────────────────────────────────────────────────────────────────────
Core metadata files in MFT
───────────────────────────────────────────────────────────────────────
$MFT, $MFTMirr, $Bitmap, $LogFile, $Volume, $AttrDef, $UpCase, ...
```

The NTFS driver is split across dedicated modules under `kernel/source/drivers/`:
`NTFS-Base.c`, `NTFS-Record.c`, `NTFS-Index.c`, `NTFS-Path.c`, `NTFS-VFS.c`, `NTFS-Time.c`, and `NTFS-Write.c`.

`MountPartition_NTFS` (`NTFS-Base.c`) validates the boot sector, checks sector-size compatibility (512/4096), computes geometry (`BytesPerSector`, `SectorsPerCluster`, `BytesPerCluster`, `MftStartCluster`, `MftStartSector`), initializes the filesystem object, and registers the mounted volume. `NtfsGetVolumeGeometry` exposes the cached geometry to diagnostics.

Low-level file-record loading is implemented by `NtfsLoadFileRecordBuffer` (`NTFS-Record.c`), which reads raw MFT records, applies update-sequence fixups, and returns a validated in-memory record image. Parsed record metadata is exposed through `NtfsReadFileRecord`.

Record attribute parsing is table-driven in `NtfsParseFileRecordAttributes` (`NTFS-Record.c`). Dedicated handlers process `FILE_NAME`, default `DATA`, `OBJECT_IDENTIFIER`, and `SECURITY_DESCRIPTOR` attributes. Stream reads are provided by `NtfsReadFileDataByIndex` and `NtfsReadFileDataRangeByIndex`; non-resident runlist reads are handled by `NtfsReadNonResidentDataAttributeRange`.

Folder traversal is implemented by `NtfsEnumerateFolderByIndex` (`NTFS-Index.c`) using `INDEX_ROOT`, `INDEX_ALLOCATION`, and `BITMAP` metadata to walk NTFS index entries. Path resolution is implemented by `NtfsResolvePathToIndex` (`NTFS-Path.c`) with case-insensitive matching and a lookup cache (`NTFSFILESYSTEM.PathLookupCache`) to reduce repeated lookups.

VFS integration is implemented in `NTFS-VFS.c` and dispatched from `NTFS-Base.c` through `DF_FS_OPENFILE`, `DF_FS_OPENNEXT`, `DF_FS_CLOSEFILE`, `DF_FS_READ`, and `DF_FS_WRITE`. NTFS metadata is translated into generic `FILE` fields and attributes (folder flag, sizes, timestamps). The current mode is read-only: write and mutating operations are routed to explicit placeholders in `NTFS-Write.c` and return `DF_RETURN_NO_PERMISSION`.

Timestamp conversion from NTFS 100ns units to kernel `DATETIME` is implemented by `NtfsTimestampToDateTime` (`NTFS-Time.c`). UTF-16LE filename decoding and comparison support is provided by `kernel/source/utils/Unicode.c` (`Utf16LeNextCodePoint`, `Utf16LeToUtf8`, `Utf16LeCompareCaseInsensitiveAscii`).


## Interaction and Networking

### Shell scripting

#### Persistent interpreter context

`InitShellContext()` creates one `SCRIPT_CONTEXT` per shell context with callbacks for output, command execution, variable resolution, and function calls. The same context is reused for command-line execution, startup commands, and `.e0` file execution until `DeinitShellContext()` destroys it.

This gives a stable interpreter state across commands and keeps callback wiring centralized in one place (`kernel/source/shell/Shell-Commands.c`).

The script engine implementation is split into dedicated modules under `kernel/source/script/` (`Script-Core.c`, `Script-Parser-Expression.c`, `Script-Parser-Statements.c`, `Script-Eval.c`, `Script-Collections.c`, `Script-Scope.c`) with public and internal headers under `kernel/include/script/`.

#### Execution paths

Shell command lines are executed through `ExecuteCommandLine()`, which calls `ScriptExecute()` directly on the entered text. Errors are reported through `ScriptGetErrorMessage()`.

Startup automation (`ExecuteStartupCommands()`) loads `Run.<index>.Command` entries from `exos.toml` and executes each entry through the same `ExecuteCommandLine()` path.

The `run` command delegates launch to `SpawnExecutable()`:

- if the resolved target ends with `.e0` (`ScriptIsE0FileName()`), `RunScriptFile()` opens the file, reads it to memory, and executes its content with `ScriptExecute()`;
- otherwise, it follows the process spawn path.

Background mode is blocked for `.e0` scripts.

#### String operators

E0 expressions support string-specific operator behavior in the interpreter:

- `string + string` concatenates both operands.
- `string - string` removes every occurrence of the right operand from the left operand.

Examples: `"foo" + "bar"` gives `"foobar"` and `"foobarfoo" - "foo"` gives `"bar"`.

#### Shell command bridge inside E0

The parser supports shell-style command statements inside E0 source. When a statement is recognized as a shell command, the AST expression node is marked `IsShellCommand = TRUE` and stores the full command line.

At evaluation time, this node calls the `ExecuteCommand` callback (`ShellScriptExecuteCommand()` in shell integration). That callback routes to:

- built-in shell commands from `COMMANDS[]`;
- executable launch via `SpawnExecutable()` when no built-in matches.

This keeps command execution policy inside shell code while the script engine stays generic.

#### Return value behavior

`AST_RETURN` stores a return value in the script context (`ScriptStoreReturnValue()`). The shell path (`RunScriptFile()`) prints it as `Script return value: ...` after successful execution.

Supported stored return categories are scalar values (string, integer, float). Host handles and arrays are rejected as return values by the interpreter storage path.

#### Host object exposure model

The shell registers host symbols with `ScriptRegisterHostSymbol()` during context initialization. Registered roots include:

- `process`
- `drivers`
- `storage`
- `pci_bus`
- `pci_device`

Each symbol is associated with a `SCRIPT_HOST_DESCRIPTOR` implemented under `kernel/source/expose/*`. Descriptor callbacks (`GetProperty`, `GetElement`) provide typed access to fields and arrays.

Access control is enforced in exposure helpers through shared macros and checks (`EXPOSE_REQUIRE_ACCESS(...)`, `ExposeCanReadProcess(...)`) so scripts can inspect kernel state through controlled interfaces instead of raw object access.


### Network Stack

EXOS implements a modern layered network stack with per-device context isolation and support for Ethernet, ARP, IPv4, and TCP protocols. The implementation follows standard networking principles with clear separation between layers and full support for multiple network devices.

#### Architecture Overview

The network stack is organized in five main layers with per-device context management:

```
┌─────────────────────────────────────┐
│            Applications             │
├─────────────────────────────────────┤
│         Socket Layer (TCP)          │
│    (Connection management, state    │
│     machine, send/receive buffers)  │
├─────────────────────────────────────┤
│          IPv4 Protocol Layer        │
│    (ICMP, UDP, TCP protocols)       │
│    [Per-device IPv4 contexts]       │
├─────────────────────────────────────┤
│             ARP Layer               │
│    (Address Resolution Protocol)    │
│     [Per-device ARP contexts]       │
├─────────────────────────────────────┤
│         Network Manager Layer       │
│  (Device discovery, initialization, │
│   callback routing, maintenance)    │
├─────────────────────────────────────┤
│           Ethernet Layer            │
│         (E1000 Driver)              │
└─────────────────────────────────────┘
```

#### Device Infrastructure

**Location:** `kernel/include/Device.h`, `kernel/source/Device.c`

The network stack uses a device-based architecture where all network devices inherit from a common `DEVICE` structure that supports context storage and management. Every device embeds a mutex used to serialize access to shared state; drivers must call `InitMutex()` on the device instance before exposing it to other subsystems.

**Device Structure:**
```c
#define DEVICE_FIELDS       \
    LISTNODE_FIELDS         \
    MUTEX Mutex;            \
    LPDRIVER Driver;        \
    LIST Contexts;

typedef struct DeviceTag {
    DEVICE_FIELDS
} DEVICE, *LPDEVICE;
```

**Context Management API:**
- `GetDeviceContext(Device, ID)`: Retrieve context by type ID
- `SetDeviceContext(Device, ID, Context)`: Store context for device
- `RemoveDeviceContext(Device, ID)`: Remove and free context

#### Device Interrupt Infrastructure

**Location:** `kernel/source/drivers/DeviceInterrupt.c`, `kernel/include/drivers/DeviceInterrupt.h`, `kernel/source/DeferredWork.c`

The device interrupt layer centralizes vector assignment, interrupt routing, and deferred work dispatching for hardware devices.

**Key Features:**
- Configurable interrupt vector slots shared across PCI/PIC paths (`General.DeviceInterruptSlots`, 1–32, default 32).
- Slot bookkeeping is allocated dynamically from kernel memory so the table matches the configured slot count.
- `DeviceInterruptRegister()` binds ISR top halves, deferred callbacks, and optional poll routines to a slot.
- `DeferredWorkDispatcher` waits on a kernel event, running deferred callbacks when signaled and invoking poll routines on timeout or when global polling mode is forced.
- Automatic spurious-interrupt suppression masks a slot after repeated suppressed top halves and relies on its poll routine until the driver re-arms the IRQ.
- Graceful fallback to polling when hardware interrupts are unavailable.
- The IOAPIC driver is optional; when ACPI is unavailable the kernel continues in PIC mode and boots without IOAPIC.
- Local APIC initialization enables the APIC and programs the spurious vector (SVR bit 8) early for consistent delivery.
- When PIC mode is active, the IMCR is forced to route legacy IRQs to the PIC.

**API Functions:**
- `InitializeDeviceInterrupts()`: Reset slot bookkeeping at boot.
- `DeviceInterruptRegister()/DeviceInterruptUnregister()`: Manage slot lifetime.
- `DeviceInterruptHandler(slot)`: ASM entry point fan-out for interrupt vectors 0x30–0x37.
- `InitializeDeferredWork()`: Start the dispatcher kernel task and supporting event.
- PIC mode remaps IRQs to vectors 0x20–0x2F before interrupts are enabled.
- PIC routing consults the IMCR presence flag set at initialization; if the register is not writable, the Local APIC LINT0 ExtINT path is enabled to keep legacy IRQs flowing.

#### Network Manager

**Location:** `kernel/source/network/NetworkManager.c`, `kernel/include/network/NetworkManager.h`

The Network Manager provides centralized network device discovery, initialization, and maintenance.

**Key Features:**
- Automatic PCI network device discovery (up to 8 devices)
- Per-device network stack initialization (ARP, IPv4, TCP)
- Unified frame reception callback routing
- Integration with the deferred work dispatcher for interrupt-driven receive paths with polling fallback
- Primary device selection for global protocols

**Initialization Flow:**
```c
void InitializeNetworkManager(void) {
    // 1. Scan PCI devices for DRIVER_TYPE_NETWORK
    // 2. For each network device:
    //    a. Reset device hardware
    //    b. Initialize ARP context
    //    c. Initialize IPv4 context
    //    d. Install device-specific RX callback
    //    e. Initialize TCP (once globally)
}
```

**API Functions:**
- `InitializeNetworkManager()`: Discover and initialize all network devices
- `NetworkManager_InitializeDevice()`: Initialize specific network device
- `NetworkManager_MaintenanceTick()`: Deferred maintenance routine invoked by `DeferredWorkDispatcher`
- `NetworkManager_GetPrimaryDevice()`: Get primary device for TCP

**DHCP Integration**
- DHCP ACK applies assigned IP, subnet mask, gateway, and DNS server to the IPv4 layer and network device context.
- ARP cache and pending IPv4 routes are flushed on lease changes before marking the device ready, ensuring stale mappings are dropped when a lease is renewed or replaced.
- DHCP retry backoff is capped; on exhaustion, the stack optionally falls back to the configured static IP/mask/gateway before declaring the device ready.

#### E1000 Ethernet Driver

**Location:** `kernel/source/network/E1000.c`

The E1000 driver provides the hardware abstraction layer for Intel 82540EM network cards. It implements the standard EXOS driver interface with network-specific function IDs.

**Key Features:**
- TX/RX descriptor ring management
- Hardware interrupt handling (IRQ 11)
- Frame transmission and reception
- EthType recognition (IPv4: 0x0800, ARP: 0x0806)
- MAC address retrieval
- Link status monitoring

**Driver Interface:**
- `DF_NT_RESET`: Reset network adapter
- `DF_NT_GETINFO`: Get MAC address and link status
- `DF_NT_SEND`: Send Ethernet frame
- `DF_NT_POLL`: Poll receive ring for new frames
- `DF_NT_SETRXCB`: Register frame receive callback
- `DF_DEV_ENABLE_INTERRUPT`: Configure interrupt routing and unmask device interrupts
- `DF_DEV_DISABLE_INTERRUPT`: Mask device interrupts and release routing

#### ARP (Address Resolution Protocol)

**Location:** `kernel/source/network/ARP.c`, `kernel/include/network/ARP.h`, `kernel/include/ARPContext.h`

ARP handles IPv4-to-MAC address resolution with per-device cache management and automatic request generation.

**Per-Device Context:**
```c
typedef struct ArpContextTag {
    LPDEVICE Device;
    U8 LocalMacAddress[6];
    U32 LocalIPv4_Be;
    ArpCacheEntry Cache[ARP_CACHE_SIZE];
} ArpContext, *LPArpContext;
```

**Key Features:**
- 32-entry LRU cache per device with TTL (10 minutes default)
- Automatic ARP request generation for unknown addresses
- ARP reply processing and cache updates
- Response to incoming ARP requests for local IP
- Paced request retransmission (3-second intervals)

**Cache Entry Structure:**
```c
typedef struct ArpCacheEntryTag {
    U32 IPv4_Be;        // IPv4 address (big-endian)
    U8 MacAddress[6];   // Corresponding MAC address
    U32 TimeToLive;     // Entry expiration timer
    U8 IsValid;         // Entry validity flag
    U8 IsProbing;       // Request already sent flag
} ArpCacheEntry;
```

**API Functions:**
- `ARP_Initialize(Device, LocalIPv4_Be, DeviceInfo)`: Initialize ARP context for device, optionally using cached link information
- `ARP_Destroy(Device)`: Cleanup ARP context
- `ARP_Resolve(Device, TargetIPv4_Be, OutMacAddress[])`: Resolve IPv4 to MAC
- `ARP_Tick(Device)`: Age cache entries (call every 1 second)
- `ARP_OnEthernetFrame(Device, Frame, Length)`: Process incoming ARP packets
- `ARP_DumpCache(Device)`: Debug helper to display cache contents

#### IPv4 Internet Protocol

**Location:** `kernel/source/network/IPv4.c`, `kernel/include/network/IPv4.h`

IPv4 layer provides packet parsing, routing, and protocol multiplexing with per-device protocol handler registration.

**Per-Device Context:**
```c
typedef struct IPv4ContextTag {
    LPDEVICE Device;
    U32 LocalIPv4_Be;
    IPv4_ProtocolHandler ProtocolHandlers[IPV4_MAX_PROTOCOLS];
} IPv4Context, *LPIPv4Context;
```

**Key Features:**
- Complete IPv4 header validation (version, IHL, checksum, TTL)
- Simple routing: local delivery vs. drop (no forwarding)
- Per-device protocol handler registration (ICMP=1, TCP=6, UDP=17)
- Automatic packet encapsulation to Ethernet
- Fragmentation detection (non-fragmented packets only)
- Checksum calculation and verification

**IPv4 Header Structure:**
```c
typedef struct IPv4HeaderTag {
    U8 VersionIHL;          // Version (4 bits) + IHL (4 bits)
    U8 TypeOfService;       // DSCP/ToS field
    U16 TotalLength;        // Total packet length (big-endian)
    U16 Identification;     // Fragment identification
    U16 FlagsFragmentOffset; // Flags + Fragment offset
    U8 TimeToLive;          // TTL hop count
    U8 Protocol;            // Next protocol number
    U16 HeaderChecksum;     // Header checksum
    U32 SourceAddress;      // Source IPv4 (big-endian)
    U32 DestinationAddress; // Destination IPv4 (big-endian)
} IPv4Header;
```

**Routing Logic:**
1. Validate packet structure and checksum
2. Check if destination matches device's local IP address
3. If local: dispatch to device's registered protocol handler
4. If remote: drop packet (no forwarding implemented)

**API Functions:**
- `IPv4_Initialize(Device, LocalIPv4_Be)`: Initialize IPv4 context for device
- `IPv4_Destroy(Device)`: Cleanup IPv4 context
- `IPv4_SetLocalAddress(Device, LocalIPv4_Be)`: Update device's local IP
- `IPv4_RegisterProtocolHandler(Device, Protocol, Handler)`: Register protocol handler
- `IPv4_Send(Device, DestinationIP, Protocol, Payload, Length)`: Send IPv4 packet
- `IPv4_OnEthernetFrame(Device, Frame, Length)`: Process incoming IPv4 packets

#### TCP (Transmission Control Protocol)

**Location:** `kernel/source/network/TCP.c`, `kernel/include/network/TCP.h`

TCP provides reliable connection-oriented communication using a state machine-based implementation.

**Key Features:**
- RFC 793 compliant state machine (CLOSED, LISTEN, SYN_SENT, ESTABLISHED, etc.)
- Connection management with unique 4-tuple identification
- Send/receive buffers with flow control
- Configurable buffer sizes through `TCP.SendBufferSize` and `TCP.ReceiveBufferSize`
- Sequence number management
- Timer-based retransmission and TIME_WAIT handling
- Checksum validation with IPv4 pseudo-header

**Connection Structure:**
```c
typedef struct TCPConnectionTag {
    // Connection identification
    U32 LocalIP;            // Local IP address (network byte order)
    U16 LocalPort;          // Local port (network byte order)
    U32 RemoteIP;           // Remote IP address (network byte order)
    U16 RemotePort;         // Remote port (network byte order)

    // Sequence numbers
    U32 SendNext;           // Next sequence number to send
    U32 SendUnacked;        // Oldest unacknowledged sequence number
    U32 RecvNext;           // Next expected sequence number

    // Window management
    U16 SendWindow;         // Send window size
    U16 RecvWindow;         // Receive window size

    // Buffers
    U8 SendBuffer[TCP_SEND_BUFFER_SIZE];
    UINT SendBufferUsed;
    UINT SendBufferCapacity;
    U8 RecvBuffer[TCP_RECV_BUFFER_SIZE];
    UINT RecvBufferUsed;
    UINT RecvBufferCapacity;

    // State machine
    STATE_MACHINE StateMachine;

    // Timers
    U32 RetransmitTimer;
    U32 TimeWaitTimer;
} TCPConnection;
```

**API Functions:**
- `TCP_Initialize()`: Initialize global TCP subsystem
- `TCP_CreateConnection(LocalIP, LocalPort, RemoteIP, RemotePort)`: Create connection
- `TCP_Connect(ConnectionID)`: Initiate active connection (SYN)
- `TCP_Listen(ConnectionID)`: Set connection to listen state
- `TCP_Send(ConnectionID, Data, Length)`: Send data
- `TCP_Receive(ConnectionID, Buffer, BufferSize)`: Receive data
- `TCP_Close(ConnectionID)`: Close connection
- `TCP_GetState(ConnectionID)`: Get current connection state
- `TCP_Update()`: Process timers and retransmissions
- `TCP_OnIPv4Packet()`: Handle incoming TCP packets (IPv4 protocol handler)

The buffer capacities default to 32768 bytes each when the configuration entries are absent.

#### Layer Interactions

**Frame Reception Flow:**
1. **E1000 Hardware** receives Ethernet frame and generates interrupt
2. **E1000 Driver** copies frame to memory, calls device-specific RX callback
3. **Network Manager** callback examines EthType and dispatches:
   - `0x0806` → `ARP_OnEthernetFrame(Device, Frame, Length)`
   - `0x0800` → `IPv4_OnEthernetFrame(Device, Frame, Length)`
4. **ARP Layer** updates device cache and responds to requests
5. **IPv4 Layer** validates packet and calls device's registered protocol handler
6. **Protocol Handler** (ICMP/UDP/TCP) processes payload with source/destination IPs

**Frame Transmission Flow:**
1. **Application** calls `IPv4_Send(Device, DestinationIP, Protocol, Payload, Length)`
2. **IPv4 Layer** calls `ARP_Resolve(Device, DestinationIP, OutMacAddress[])`
3. **ARP Layer** returns cached MAC or triggers ARP request
4. **IPv4 Layer** builds Ethernet + IPv4 headers with source MAC from device context
5. **E1000 Driver** transmits frame via `DF_NT_SEND`

#### Network Configuration

**Default Configuration:**
- Primary device IP: `192.168.56.16` (big-endian: `0xC0A83810`)
- Network: `192.168.56.0/24`
- Gateway: `192.168.56.1`
- QEMU TAP interface: `tap0`

**Initialization Sequence:**
```c
// 1. Initialize Network Manager (discovers all devices)
InitializeNetworkManager();

// 2. For each device, Network Manager automatically:
//    a. Calls ARP_Initialize(Device, DEFAULT_LOCAL_IP_BE, CachedInfo)
//    b. Calls IPv4_Initialize(Device, DEFAULT_LOCAL_IP_BE)
//    c. Calls TCP_Initialize() (once globally)

// 3. Application registers protocol handlers per device:
IPv4_RegisterProtocolHandler(Device, IPV4_PROTOCOL_ICMP, ICMPHandler);
IPv4_RegisterProtocolHandler(Device, IPV4_PROTOCOL_UDP, UDPHandler);
IPv4_RegisterProtocolHandler(Device, IPV4_PROTOCOL_TCP, TCP_OnIPv4Packet);

// 4. Deferred work dispatcher drives maintenance once initialized during boot
```

#### Key Benefits of Per-Device Architecture

1. **Scalability**: Supports multiple network interfaces simultaneously
2. **Isolation**: Each device maintains independent protocol state
3. **Flexibility**: Different devices can have different IP addresses and protocol configurations
4. **Reliability**: Failure of one network device doesn't affect others
5. **Maintainability**: Clear separation of concerns and context management

The network stack successfully handles real network traffic across multiple devices and provides a robust foundation for implementing network applications and services.


## Tooling and References

### Logging

Kernel logging funnels through `KernelLogText` and uses typed prefixes for log classes. The available log types are `DEBUG`, `WARNING`, `ERROR`, `VERBOSE`, and `TEST`. `DEBUG`, `WARNING`, `ERROR`, and `VERBOSE` are always available, while `TEST` is a debug-only type used by automated test scripts and is compiled out when `DEBUG_OUTPUT` is disabled. All logs follow the standard `[FunctionName]` prefix rule and emit structured results such as `TEST > [CMD_sysinfo] sys_info : OK`. Serial output is sanitized to printable ASCII (plus tab/newline) before being written to the log. When `DEBUG_SPLIT` is set to `1`, the kernel log stream is sent to a dedicated console region on the right side of the screen while standard console output remains on the left.
`KernelLogSetTagFilter()` adds optional tag-based filtering. The filter string is a separator-based list (comma, semicolon, pipe, or spaces) and each entry matches a log prefix tag (for example `MountDiskPartitionsGpt` or `[MountDiskPartitionsGpt]`). When a filter is active, only log lines whose first bracket tag is listed are emitted. The default kernel filter is initialized for NVMe/GPT diagnosis.
The build can override this startup filter with `--kernel-log-tag-filter <value>` in `scripts/build.sh`; passing an empty value compiles an empty default filter.
The `ThresholdLatch` utility supports one-shot logging when a time threshold is exceeded during long-running operations.


### Automated debug validation script

The repository provides `scripts/4-1-smoke-test.sh` to run an automated debug validation flow:

- clean build + image generation,
- QEMU boot,
- shell command injection (`sys_info`, `dir`, `/system/apps/hello`),
- kernel log pattern checks.

The script supports selecting one target with `--only x86-32`, `--only x86-64`, or `--only x86-64-uefi`.  
Kernel logs are consumed from per-target files (`log/kernel-x86-32-mbr-debug.log`, `log/kernel-x86-64-mbr-debug.log`, `log/kernel-x86-64-uefi-debug.log` and release equivalents with `-release`).


### Build output layout

Build artifacts are split between a core folder and an image folder:

- core outputs: `build/core/<BUILD_CORE_NAME>/...`
- image outputs: `build/image/<BUILD_IMAGE_NAME>/...`
- `BUILD_CORE_NAME`: `<arch>-<boot>-<config>[-split]`
- `BUILD_IMAGE_NAME`: `<BUILD_CORE_NAME>-<filesystem>`

Examples:

- x86-32 MBR debug ext2:
  - core: `build/core/x86-32-mbr-debug`
  - image: `build/image/x86-32-mbr-debug-ext2`
- x86-64 UEFI debug ext2:
  - core: `build/core/x86-64-uefi-debug`
  - image: `build/image/x86-64-uefi-debug-ext2`

Path mapping (migration reference):

| Old path | New path |
|---|---|
| `build/x86-32/kernel/exos.elf` | `build/core/x86-32-mbr-debug/kernel/exos.elf` |
| `build/x86-64/kernel/exos.elf` | `build/core/x86-64-mbr-debug/kernel/exos.elf` |
| `build/x86-32/boot-hd/exos.img` | `build/image/x86-32-mbr-debug-ext2/boot-hd/exos.img` |
| `build/x86-64/boot-hd/exos.img` | `build/image/x86-64-mbr-debug-ext2/boot-hd/exos.img` |
| `build/x86-64/boot-uefi/exos-uefi.img` | `build/image/x86-64-uefi-debug-ext2/boot-uefi/exos-uefi.img` |
| `build/x86-32/tools/cycle` | `build/core/x86-32-mbr-debug/tools/cycle` |
| `build/x86-64/tools/cycle` | `build/core/x86-64-mbr-debug/tools/cycle` |


### Package tooling

Host-side EPK package generation is implemented in `tools/source/package/epk_pack.c`.
The binary is built by `tools/Makefile` and produced as:
- `build/core/<build-core-name>/tools/epk-pack`

Command form:
- `build/core/<build-core-name>/tools/epk-pack pack --input <folder> --output <file.epk>`


### Keyboard Layout Format (EKM1)

The EKM1 layout file describes a USB HID keyboard map using usage page 0x07. Files are UTF-8 text with a required 4-byte header "EKM1". Lines are tokenized on whitespace and comments start with `#`. Tokens are case-sensitive and directive order matters only for `levels`, which must appear before any `map` entry. The loader rejects malformed entries.

Directives:
- `code <layout_code>`: required, unique layout identifier string (example: `code en-US`).
- `levels <count>`: optional, decimal level count, range 1 to 4. Defaults to 1 when omitted.
- `map <usage_hex> <level_dec> <vk_hex> <ascii_hex> <unicode_hex>`: maps a HID usage to a keycode for a given level.
  - `usage_hex` range: 0x04 to 0xE7 (HID usage page 0x07).
  - `level_dec` range: 0 to levels-1.
  - `vk_hex` range: 0x00 to 0xFF.
  - `ascii_hex` range: 0x00 to 0xFF.
  - `unicode_hex` range: 0x0000 to 0xFFFF.
  - Each usage and level pair may appear only once.
- `dead <dead_unicode_hex> <base_unicode_hex> <result_unicode_hex>`: defines a dead key combination. Maximum 128 entries.
- `compose <first_unicode_hex> <second_unicode_hex> <result_unicode_hex>`: defines a compose sequence. Maximum 256 entries.

Recommended layout levels:
- Level 0: base.
- Level 1: shift.
- Level 2: AltGr.
- Level 3: control.

Example:
```
EKM1
# US QWERTY layout (en-US)
code en-US
levels 2
map 0x04 0 0x30 0x61 0x0061
map 0x04 1 0x30 0x41 0x0041
```


### QEMU network graph

```
[ VM (Guest OS) ]                [ QEMU Process ]                  [ Host OS / PC ]
+----------------+               +-------------------+             +----------------+
|                |               |                   |             |                |
| +------------+ |               | +---------------+ |             | +------------+ |
| | E1000 NIC  | | (Ethernet     | | SLIRP Backend | | (NAT &      | | Host NIC   | |
| | (Virtual)  | |  Packets)     | | (User Net)    | |  Routing)   | | (Physical) | |
| +------------+ | ------------> | | Mini Network  | | ----------> | | e.g. eth0  | |
|                |               | | (10.0.2.0/24) | |             | +------------+ |
|                |               | +---------------+ |             |                |
|                |               |                   |             |                |
| App/TCP/IP     |               | NAT Layer         |             | Kernel TCP/IP  |
| (L3/L4)        |               | (Translate IP)    |             | (L3/L4)        |
+----------------+               +-------------------+             +----------------+
```


### Links

| Name | Description | Link |
|------|-------------|------|
| RFCs | RFC Root | https://datatracker.ietf.org/ |
| RFC 791 | Internet protocol | https://datatracker.ietf.org/doc/html/rfc791/ |
| RFC 793 | Transmission Control Protocol | https://datatracker.ietf.org/doc/html/rfc793/ |
| Intel x86-64 | x86-64 Technical Documentation | https://www.intel.com/content/www/us/en/developer/articles/technical/intel-sdm.html |
