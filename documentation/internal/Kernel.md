# Kernel documentation

## Architecture

Work to bring the long-mode kernel build closer to parity with the i386
port continues. The x86-64 backend exposes placeholder scheduler and
context-switch helpers so common code can compile while the full task
switching path is implemented. The interrupt bootstrap has also been
rebuilt around the 16-byte IDT format, providing stubs for each vector so
the C runtime reaches the scheduler before the final entry/exit sequences
are written.

### Paging abstractions

The memory manager relies on `arch/Memory.h` to describe page hierarchy
helpers exposed by the active architecture backend. The i386
implementation (`arch/i386/i386-Memory.h`) centralizes directory and
table index calculations, exposes accessors for the self-mapped page
directory, and provides helper routines to build raw page directory and
page table entries. Kernel code constructs mappings through
`MakePageDirectoryEntryValue`, `MakePageTableEntryValue`, and
`WritePage*EntryValue` instead of touching the i386 bitfields directly.
This abstraction keeps `Memory.c` agnostic of the paging depth so that a
future x86-64 backend can extend the hierarchy without refactoring the
core allocator.

`arch/i386/i386-Memory.h` exposes a generic `ARCH_PAGE_ITERATOR`
helper that walks page mappings without assuming a fixed number of page
table levels. Region management routines (`IsRegionFree`, `AllocRegion`,
`FreeRegion`, and friends) advance the iterator rather than manually
splitting linear addresses into directory/table indexes, and table
reclamation relies on `PageTableIsEmpty`. Physical range clipping is
also delegated to the architecture via `ClipPhysicalRange`, keeping
future 64-bit backends free to extend address limits without touching the
common kernel code.

`InitializeMemoryManager` defers to `InitializeMemoryManager` so
the architecture backend owns the low-level bootstrap steps. The i386
implementation continues to reserve the bitmap in low memory, seed the
temporary mapping slots, install the recursive page directory, and load
the GDT. The x86-64 path mirrors these steps, wiring the temporary
linear aliases, building the initial PML4, and installing a long-mode
GDT so higher-half execution can begin without architecture-specific
hooks inside the generic memory manager.

On long mode builds the kernel now allocates paging structures explicitly
instead of cloning the loader tables. `AllocPageDirectory` creates fresh
low-memory and kernel PDPTs, wires the task-runner window, and programs
the recursive slot before returning the new PML4. `AllocUserPageDirectory`
reuses those helpers but also reserves an empty userland page table so
`AllocRegion` can immediately populate process space without reconstructing
the hierarchy first. The low-memory region builder keeps a cached pair of
BIOS-protected and general identity tables so new page directories only
consume fresh pages for their PDPT, directory, and any userland seed tables.

### Kernel object identifiers

Kernel objects embed a 64-bit identifier in `OBJECT_FIELDS`. The identifier
is assigned when `CreateKernelObject` allocates the structure and is derived
from a randomly generated UUID. This value travels with the object for its
entire lifetime and is persisted in the termination cache through
`OBJECT_TERMINATION_STATE.ID`. Scheduler lookups rely on the shared identifier
instead of raw pointers, eliminating accidental matches when memory is reused
for new objects.

### Command line editing

Interactive editing of shell command lines is implemented in
`kernel/source/utils/CommandLineEditor.c`. The module processes keyboard input,
maintains an in-memory history, refreshes the console display, and relies on
callbacks to retrieve completion suggestions. The shell owns an input state
structure that embeds the editor instance and provides the shell-specific
completion callback so the component remains agnostic of higher level shell
logic.

All reusable helpers —such as the command line editor, adaptive delay, string
containers, CRC utilities, notifications, path helpers, TOML parsing, UUID
support, regex, hysteresis control, and network checksum helpers— live under
`kernel/source/utils` with their public headers in `kernel/include/utils`. This
keeps generic infrastructure separated from core subsystems and makes it easier
to share common code across the kernel.

Hardware-facing components are grouped under `kernel/source/drivers` with their
headers in `kernel/include/drivers`. The directory hosts the keyboard, serial
mouse, interrupt controller (I/O APIC), PCI bus, network (E1000), storage (ATA
and SATA), graphics (VGA, VESA, and mode tables), and file system backends
(FAT16, FAT32, and EXFS). Keeping device drivers together simplifies discovery
from the build system and clarifies the separation between reusable utilities
and hardware support code.

### Shell scripting integration

The interactive shell keeps a persistent script interpreter context to run
automation snippets. Host-side data is exposed through `ScriptRegisterHostSymbol`
so scripts can inspect kernel state without bypassing the interpreter API. The
shell publishes the kernel process list under the global identifier
`process`. Scripts can iterate over the list (`process[0]`, `process[1]`, ...)
and query per-process properties such as `Status`, `Flags`, `ExitCode`,
`FileName`, `CommandLine`, and `WorkFolder`, enabling diagnostics like
`process[0].CommandLine` directly from the scripting language.

### ACPI services

Advanced power management and reset paths live in `kernel/source/ACPI.c`. The
module discovers ACPI tables, exposes the parsed configuration, and offers
helpers for platform control. `ACPIShutdown()` enters the S5 soft-off state and
falls back to legacy power-off sequences when the ACPI path fails. The new
`ACPIReboot()` companion performs a warm reboot by first using the ACPI reset
register (when present) and then chaining to legacy reset controllers to ensure
the machine restarts even on older chipsets.

### File system globals

The kernel tracks shared file system information in `Kernel.FileSystemInfo`.
It currently stores the logical name of the partition flagged as active while
MBR partitions are mounted. `MountDiskPartitions` identifies the active entry
directly from the MBR, then calls `FileSystemSetActivePartition` to copy the
mounted file system name into `Kernel.FileSystemInfo.ActivePartitionName` for
later use (for example, in the shell).

## Startup sequence on HD (real HD on i386 or qemu-system-i386)

Everything in this sequence runs in 16-bit real mode on i386+ processors.
However, the code uses 32 bit registers when appropriate.

1. BIOS loads disk MBR at 0x7C00.
2. Code in mbr.asm is executed.
3. MBR code looks for the active partition and loads its VBR at 0x7E00.
4. Code in vbr.asm is executed.
5. VBR code loads the reserved FAT32 sectors (which contain VBR payload) at 0x8000.
6. Code in vbr-payload-a.asm is executed.
7. VBR payload asm sets up a stack and calls BootMain in vbr-payload-c.c.
8. BootMain finds the FAT32 entry for the specified binary.
9. BootMain reads all the clusters of the binary at 0x20000.
10. EnterProtectedPagingAndJump sets up minimal GDT and paging structures for the loaded binary to execute in higher half memory (0xC0000000).
11. It finally jumps to the loaded binary.
12. That's all folks. But it was a real pain to code :D

## Physical Memory map (may change)

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

## Foreign File systems

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

## EXOS File System - EXFS

### Notations used in this document

| Abbrev | Meaning                |
|--------|------------------------|
| U8     | unsigned byte          |
| I8     | signed byte            |
| U16    | unsigned word          |
| I16    | signed word            |
| U32    | unsigned long          |
| I32    | signed long            |

| Abbrev | Meaning                           |
|--------|-----------------------------------|
| EXOS   | Extensible Operating System       |
| BIOS   | Basic Input/Output System         |
| CHS    | Cylinder-Head-Sector              |
| MBR    | Master Boot Record                |
| OS     | Operating System                  |

---

### Structure of the Master Boot Record

| Offset   | Type | Description                                  |
|----------|------|----------------------------------------------|
| 0..445   | U8x? | The boot sequence                            |
| 446..461 | ?    | CHS location of partition No 1               |
| 462..477 | ?    | CHS location of partition No 2               |
| 478..493 | ?    | CHS location of partition No 3               |
| 494..509 | ?    | CHS location of partition No 4               |
| 510      | U16  | BIOS signature : 0x55AA (`_*_*_*_**_*_*_*_`) |

---

### Structure of SuperBlock

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

### Structure of FileRecord

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

### FileRecord fields

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

### Structure of folders and files

- A cluster that contains 32-bit indices to other clusters is called a **page**.  
- FileRecord contains a cluster index for its first page.  
- A page is filled with cluster indices pointing to file/folder data.  
- For folders: data = series of FileRecords.  
- For files: data = arbitrary user data.  
- The **last entry** of a page is `0xFFFFFFFF`.  
- If more than one page is needed, the last index points to the **next page**.  

---

### Clusters

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

### Cluster bitmap

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

## EXT2 strcture

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


## Tasks

### Architecture-specific task data

Each task embeds an `ARCH_TASK_DATA` structure (declared in the architecture-specific header under
`kernel/include/arch/`) that contains the saved interrupt frame along with the user, system, and
any auxiliary stack descriptors that the target CPU requires. The generic `tag_TASK` definition in
`kernel/include/process/Task.h` exposes this structure as the `Arch` member so that all stack and
context manipulations remain scoped to the active architecture.

The i386 implementation of `SetupTask` (`kernel/source/arch/i386/i386.c`) is responsible for
allocating and clearing the per-task stacks, initialising the selectors in the interrupt frame and
performing the bootstrap stack switch for the main kernel task. The x86-64 flavour performs the
same duties and additionally provisions a dedicated Interrupt Stack Table (IST1) stack for faults
that require a reliable kernel stack even if the regular system stack becomes unusable. During IDT
initialisation the kernel now assigns IST1 to the fault vectors that are most likely to execute with
a corrupted task stack (double fault, invalid TSS, segment-not-present, stack, general protection
and page faults). This ensures the handlers always run on the emergency per-task stack, preventing
the double-fault escalation that previously produced a triple fault when the active stack pointer
was already invalid.
`CreateTask` calls the relevant helper after finishing the generic bookkeeping, which keeps the
scheduler and task manager architecture-agnostic while allowing future architectures to provide
their own `SetupTask` specialisation.

Both the i386 and x86-64 context-switch helpers (`SetupStackForKernelMode` and
`SetupStackForUserMode` in their respective architecture headers) must reserve space on the stack in
bytes rather than entries before writing the return frame. Subtracting the correct byte count avoids
writing past the top of the allocated stack when seeding the initial `iret` frame for a task. On
 x86-64 the helpers also arrange the bootstrap frame so that the stack pointer becomes 16-byte
 aligned after `iretq` pops its arguments, preserving the ABI-mandated alignment once execution
 resumes in the scheduled task.

### Stack sizing

The minimum sizes for task and system stacks are driven by the configuration keys
`Task.MinimumTaskStackSize` and `Task.MinimumSystemStackSize` in `kernel/configuration/exos.ref.toml`.
At boot the task manager reads those values, but it clamps them to the architecture defaults
(`64 KiB`/`16 KiB` on i386 and `128 KiB`/`32 KiB` on x86-64) to prevent under-provisioned stacks.
Increasing the values in the configuration grows every newly created task and keeps the auto stack
growing logic operating on the larger baseline.

### IRQ scheduling

#### IRQ 0 path

IRQ 0
└── trap lands in interrupt-a.asm : Interrupt_Clock
    └── calls ClockHandler to increment system time
    └── calls Scheduler to check if it's time to switch to another task
        └── Scheduler switches page directory if needed and returns the next task's context

#### ISR 0 call graph

Interrupt_Clock
└── BuildInterruptFrame
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

## System calls

### System call full path - i386

exos-runtime-c.c : malloc() (or any other function)
└── calls exos-runtime-a.asm : exoscall()
    └── calls int EXOS_USER_CALL
        └── trap lands in interrupt-a.asm : Interrupt_SystemCall
            └── calls SYSCall.c : SystemCallHandler()
                └── calls SysCall_xxx via SysCallTable[]
                    └── whew... finally job is done

### System call full path - x86-64

exos-runtime-c.c : malloc() (or any other function)
└── calls exos-runtime-a.asm : exoscall()
    └── syscall instruction
        └── syscall lands in interrupt-a.asm : Interrupt_SystemCall
            └── calls SYSCall.c : SystemCallHandler()
                └── calls SysCall_xxx via SysCallTable[]
                    └── whew... finally job is done

## Process and Task Lifecycle Management

EXOS implements a lifecycle management system for both processes and tasks that ensures consistent cleanup and prevents resource leaks.

### Process Heap Management

- Every `PROCESS` keeps track of its `MaximumAllocatedMemory`, which is initialized to `N_HalfMemory` for both the kernel and user processes.
- When a heap allocation exhausts the committed region, the kernel automatically attempts to double the heap size without exceeding the process limit by calling `ResizeRegion`.
- If the resize operation cannot be completed, the allocator logs an error and the allocation fails gracefully.

### Status States

**Task Status (Task.Status):**
- `TASK_STATUS_FREE` (0x00): Unused task slot
- `TASK_STATUS_READY` (0x01): Ready to run
- `TASK_STATUS_RUNNING` (0x02): Currently executing
- `TASK_STATUS_WAITING` (0x03): Waiting for an event
- `TASK_STATUS_SLEEPING` (0x04): Sleeping for a specific time
- `TASK_STATUS_WAITMESSAGE` (0x05): Waiting for a message
- `TASK_STATUS_DEAD` (0xFF): Marked for deletion

**Process Status (Process.Status):**
- `PROCESS_STATUS_ALIVE` (0x00): Normal operating state
- `PROCESS_STATUS_DEAD` (0xFF): Marked for deletion

### Process Creation Flags

**Process Creation Flags (Process.Flags):**
- `PROCESS_CREATE_TERMINATE_CHILD_PROCESSES_ON_DEATH` (0x00000001): When the process terminates, all child processes are also killed. If this flag is not set, child processes are orphaned (their Parent field is set to NULL).

### Lifecycle Flow

**1. Task Termination:**
- When a task terminates, `KillTask()` releases every mutex held by the task
  before marking it as `TASK_STATUS_DEAD`
- The task remains in the scheduler queue until the next context switch
- Voluntary exits (`SysCall_Exit`) immediately run the scheduler so the
  terminating task never resumes execution in user mode
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

### Key Design Principles

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

## Network Stack

EXOS implements a modern layered network stack with per-device context isolation and support for Ethernet, ARP, IPv4, and TCP protocols. The implementation follows standard networking principles with clear separation between layers and full support for multiple network devices.

### Architecture Overview

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

### Device Infrastructure

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

### Network Manager

**Location:** `kernel/source/network/NetworkManager.c`, `kernel/include/network/NetworkManager.h`

The Network Manager provides centralized network device discovery, initialization, and maintenance.

**Key Features:**
- Automatic PCI network device discovery (up to 8 devices)
- Per-device network stack initialization (ARP, IPv4, TCP)
- Unified frame reception callback routing
- Periodic maintenance (RX polling, ARP aging, TCP timers)
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
- `NetworkManagerTask()`: Periodic maintenance task (polling, timers)
- `NetworkManager_GetPrimaryDevice()`: Get primary device for TCP

### E1000 Ethernet Driver

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

### ARP (Address Resolution Protocol)

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

### IPv4 Internet Protocol

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

### TCP (Transmission Control Protocol)

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

### Layer Interactions

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

### Network Configuration

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

// 4. Start Network Manager maintenance task:
CreateTask(&KernelProcess, NetworkManagerTask);
```

### Key Benefits of Per-Device Architecture

1. **Scalability**: Supports multiple network interfaces simultaneously
2. **Isolation**: Each device maintains independent protocol state
3. **Flexibility**: Different devices can have different IP addresses and protocol configurations
4. **Reliability**: Failure of one network device doesn't affect others
5. **Maintainability**: Clear separation of concerns and context management

The network stack successfully handles real network traffic across multiple devices and provides a robust foundation for implementing network applications and services.

## QEMU Network

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

## Links

RFCs        	https://datatracker.ietf.org/
RFC 791     	Internet protocol               https://datatracker.ietf.org/doc/html/rfc791/
RFC 793     	Transmission Control Protocol   https://datatracker.ietf.org/doc/html/rfc793/
Intel x86-64	https://www.intel.com/content/www/us/en/developer/articles/technical/intel-sdm.html
