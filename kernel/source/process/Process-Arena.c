/************************************************************************\

    EXOS Kernel
    Copyright (c) 1999-2026 Jango73

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <https://www.gnu.org/licenses/>.


    Process address space arenas

\************************************************************************/

#include "process/Process-Arena.h"

#include "Log.h"
#include "Memory.h"
#include "CoreString.h"
#include "process/Process.h"

/************************************************************************/

#define PROCESS_ARENA_SYSTEM_RESERVED N_16MB
#define PROCESS_ARENA_MMIO_RESERVED N_16MB
#define PROCESS_ARENA(Process, Id) (&((Process)->AddressSpace.Ranges[(Id)]))

/************************************************************************/

static LINEAR ProcessArenaAlignUp(LINEAR Value) {
    if ((Value & (PAGE_SIZE - 1)) == 0) {
        return Value;
    }

    return (Value + PAGE_SIZE) & PAGE_MASK;
}

/************************************************************************/

static LINEAR ProcessArenaAlignDown(LINEAR Value) {
    return Value & PAGE_MASK;
}

/************************************************************************/

static void ProcessArenaRangeInitialize(LPPROCESS_ARENA_RANGE Range, LINEAR Base, LINEAR Limit) {
    if (Range == NULL) {
        return;
    }

    Range->Base = Base;
    Range->Limit = Limit;
    Range->NextLow = Base;
    Range->NextHigh = Limit;
}

/************************************************************************/

static BOOL ProcessArenaRangeContains(LPPROCESS_ARENA_RANGE Range, LINEAR Address, UINT Size) {
    LINEAR End;

    if (Range == NULL || Address == 0 || Size == 0) {
        return FALSE;
    }

    End = Address + Size;

    if (End < Address) {
        return FALSE;
    }

    if (Address < Range->Base) {
        return FALSE;
    }

    if (Range->Limit != 0 && End > Range->Limit) {
        return FALSE;
    }

    return TRUE;
}

/************************************************************************/

/**
 * @brief Reset all process arena descriptors.
 */
void ProcessArenaReset(LPPROCESS Process) {
    SAFE_USE_VALID_ID(Process, KOID_PROCESS) {
        MemorySet(&(Process->AddressSpace), 0, sizeof(Process->AddressSpace));
    }
}

/************************************************************************/

/**
 * @brief Initialize kernel process arenas.
 */
BOOL ProcessArenaInitializeKernel(LPPROCESS Process) {
    SAFE_USE_VALID_ID(Process, KOID_PROCESS) {
        ProcessArenaReset(Process);

        ProcessArenaRangeInitialize(PROCESS_ARENA(Process, PROCESS_ARENA_IMAGE), 0, 0);
        ProcessArenaRangeInitialize(PROCESS_ARENA(Process, PROCESS_ARENA_HEAP), Process->HeapBase, 0);
        ProcessArenaRangeInitialize(PROCESS_ARENA(Process, PROCESS_ARENA_STACK), 0, 0);
        ProcessArenaRangeInitialize(PROCESS_ARENA(Process, PROCESS_ARENA_SYSTEM), VMA_KERNEL, 0);
        ProcessArenaRangeInitialize(PROCESS_ARENA(Process, PROCESS_ARENA_MMIO), VMA_KERNEL, 0);

        Process->AddressSpace.Initialized = TRUE;
        return TRUE;
    }

    return FALSE;
}

/************************************************************************/

/**
 * @brief Initialize user process arenas.
 */
BOOL ProcessArenaInitializeUser(
    LPPROCESS Process,
    LINEAR ImageBase,
    UINT ImageSize,
    LINEAR HeapBase,
    UINT InitialHeapSize) {
    LINEAR UserLimit;
    LINEAR ImageLimit;
    LINEAR HeapStart;
    LINEAR HeapInitialEnd;
    LINEAR SystemBase;
    LINEAR MmioBase;
    LINEAR StackBase;

    SAFE_USE_VALID_ID(Process, KOID_PROCESS) {
        if (ImageSize == 0 || InitialHeapSize == 0) {
            ERROR(TEXT("[ProcessArenaInitializeUser] Invalid image/heap size (ImageSize=%u InitialHeapSize=%u)"),
                  ImageSize,
                  InitialHeapSize);
            return FALSE;
        }

        ImageBase = ProcessArenaAlignDown(ImageBase);
        ImageLimit = ProcessArenaAlignUp(ImageBase + ImageSize);
        HeapStart = ProcessArenaAlignDown(HeapBase);
        HeapInitialEnd = ProcessArenaAlignUp(HeapBase + InitialHeapSize);
        UserLimit = ProcessArenaAlignDown(VMA_TASK_RUNNER);

        if (HeapStart < ImageLimit || HeapInitialEnd <= HeapStart || UserLimit <= HeapInitialEnd) {
            ERROR(TEXT("[ProcessArenaInitializeUser] Invalid user ranges Image=[%p,%p) Heap=[%p,%p) UserLimit=%p"),
                  ImageBase,
                  ImageLimit,
                  HeapStart,
                  HeapInitialEnd,
                  UserLimit);
            return FALSE;
        }

        if (UserLimit <= PROCESS_ARENA_SYSTEM_RESERVED + PROCESS_ARENA_MMIO_RESERVED) {
            ERROR(TEXT("[ProcessArenaInitializeUser] User linear space too small"));
            return FALSE;
        }

        SystemBase = ProcessArenaAlignDown(UserLimit - PROCESS_ARENA_SYSTEM_RESERVED);
        MmioBase = ProcessArenaAlignDown(SystemBase - PROCESS_ARENA_MMIO_RESERVED);
        StackBase = HeapInitialEnd;

        if (MmioBase <= StackBase) {
            ERROR(TEXT("[ProcessArenaInitializeUser] Not enough stack/system room (StackBase=%p MmioBase=%p)"),
                  StackBase,
                  MmioBase);
            return FALSE;
        }

        ProcessArenaReset(Process);

        ProcessArenaRangeInitialize(PROCESS_ARENA(Process, PROCESS_ARENA_IMAGE), ImageBase, HeapStart);
        ProcessArenaRangeInitialize(PROCESS_ARENA(Process, PROCESS_ARENA_HEAP), HeapStart, MmioBase);
        ProcessArenaRangeInitialize(PROCESS_ARENA(Process, PROCESS_ARENA_STACK), StackBase, MmioBase);
        ProcessArenaRangeInitialize(PROCESS_ARENA(Process, PROCESS_ARENA_MMIO), MmioBase, SystemBase);
        ProcessArenaRangeInitialize(PROCESS_ARENA(Process, PROCESS_ARENA_SYSTEM), SystemBase, UserLimit);

        Process->AddressSpace.Initialized = TRUE;
        return TRUE;
    }

    return FALSE;
}

/************************************************************************/

/**
 * @brief Allocate a block in the process system arena.
 */
LINEAR ProcessArenaAllocateSystem(LPPROCESS Process, UINT Size, U32 Flags, LPCSTR Tag) {
    LINEAR AllocationBase;
    LINEAR Result;
    UINT AlignedSize;

    SAFE_USE_VALID_ID(Process, KOID_PROCESS) {
        if (Process->AddressSpace.Initialized == FALSE || Size == 0) {
            return 0;
        }

        AlignedSize = ProcessArenaAlignUp(Size);
        AllocationBase = ProcessArenaAlignUp(PROCESS_ARENA(Process, PROCESS_ARENA_SYSTEM)->NextLow);

        if (PROCESS_ARENA(Process, PROCESS_ARENA_SYSTEM)->Limit != 0 &&
            AllocationBase + AlignedSize > PROCESS_ARENA(Process, PROCESS_ARENA_SYSTEM)->Limit) {
            ERROR(TEXT("[ProcessArenaAllocateSystem] Arena exhausted for process %p"), Process);
            return 0;
        }

        Result = AllocRegion(AllocationBase,
                             0,
                             AlignedSize,
                             Flags | ALLOC_PAGES_AT_OR_OVER,
                             Tag);
        if (Result == 0) {
            ERROR(TEXT("[ProcessArenaAllocateSystem] AllocRegion failed for process %p (Base=%p Size=%u)"),
                  Process,
                  AllocationBase,
                  AlignedSize);
            return 0;
        }

        if (ProcessArenaRangeContains(PROCESS_ARENA(Process, PROCESS_ARENA_SYSTEM), Result, AlignedSize) == FALSE) {
            ERROR(TEXT("[ProcessArenaAllocateSystem] Out-of-range allocation %p (size=%u) for process %p"),
                  Result,
                  AlignedSize,
                  Process);
            FreeRegion(Result, AlignedSize);
            return 0;
        }

        PROCESS_ARENA(Process, PROCESS_ARENA_SYSTEM)->NextLow = ProcessArenaAlignUp(Result + AlignedSize);
        return Result;
    }

    return 0;
}

/************************************************************************/

/**
 * @brief Allocate a block in the process MMIO arena.
 */
LINEAR ProcessArenaAllocateMmio(LPPROCESS Process, PHYSICAL Target, UINT Size, U32 Flags, LPCSTR Tag) {
    LINEAR AllocationBase;
    LINEAR Result;
    UINT AlignedSize;
    U32 EffectiveFlags = Flags | ALLOC_PAGES_IO;

    SAFE_USE_VALID_ID(Process, KOID_PROCESS) {
        if (Process->AddressSpace.Initialized == FALSE || Size == 0) {
            return 0;
        }

        AlignedSize = ProcessArenaAlignUp(Size);
        AllocationBase = ProcessArenaAlignUp(PROCESS_ARENA(Process, PROCESS_ARENA_MMIO)->NextLow);

        if (PROCESS_ARENA(Process, PROCESS_ARENA_MMIO)->Limit != 0 &&
            AllocationBase + AlignedSize > PROCESS_ARENA(Process, PROCESS_ARENA_MMIO)->Limit) {
            ERROR(TEXT("[ProcessArenaAllocateMmio] Arena exhausted for process %p"), Process);
            return 0;
        }

        Result = AllocRegion(AllocationBase,
                             Target,
                             AlignedSize,
                             EffectiveFlags | ALLOC_PAGES_AT_OR_OVER,
                             Tag);
        if (Result == 0) {
            ERROR(TEXT("[ProcessArenaAllocateMmio] AllocRegion failed for process %p (Base=%p Size=%u)"),
                  Process,
                  AllocationBase,
                  AlignedSize);
            return 0;
        }

        if (ProcessArenaRangeContains(PROCESS_ARENA(Process, PROCESS_ARENA_MMIO), Result, AlignedSize) == FALSE) {
            ERROR(TEXT("[ProcessArenaAllocateMmio] Out-of-range allocation %p (size=%u) for process %p"),
                  Result,
                  AlignedSize,
                  Process);
            FreeRegion(Result, AlignedSize);
            return 0;
        }

        PROCESS_ARENA(Process, PROCESS_ARENA_MMIO)->NextLow = ProcessArenaAlignUp(Result + AlignedSize);
        return Result;
    }

    return 0;
}

/************************************************************************/

/**
 * @brief Allocate a user stack from the top of the stack arena.
 */
LINEAR ProcessArenaAllocateUserStack(LPPROCESS Process, UINT Size) {
    LINEAR MinimumBase;
    LINEAR Candidate;
    LINEAR Result = 0;
    UINT AlignedSize;

    SAFE_USE_VALID_ID(Process, KOID_PROCESS) {
        if (Process->AddressSpace.Initialized == FALSE || Size == 0) {
            return 0;
        }

        AlignedSize = ProcessArenaAlignUp(Size);
        MinimumBase = ProcessArenaAlignUp(PROCESS_ARENA(Process, PROCESS_ARENA_STACK)->Base);

        if (Process->HeapBase != 0 && Process->HeapSize != 0) {
            LINEAR HeapEnd = ProcessArenaAlignUp(Process->HeapBase + Process->HeapSize);
            if (HeapEnd > MinimumBase) {
                MinimumBase = HeapEnd;
            }
        }

        Candidate = ProcessArenaAlignDown(PROCESS_ARENA(Process, PROCESS_ARENA_STACK)->NextHigh);
        if (Candidate > PROCESS_ARENA(Process, PROCESS_ARENA_STACK)->Limit) {
            Candidate = PROCESS_ARENA(Process, PROCESS_ARENA_STACK)->Limit;
        }

        while (Candidate >= MinimumBase + AlignedSize) {
            LINEAR Base = Candidate - AlignedSize;
            Result = AllocRegion(Base,
                                 0,
                                 AlignedSize,
                                 ALLOC_PAGES_COMMIT | ALLOC_PAGES_READWRITE,
                                 TEXT("TaskStack"));
            if (Result != 0) {
                break;
            }

            Candidate = Base;
        }

        if (Result == 0) {
            ERROR(TEXT("[ProcessArenaAllocateUserStack] No stack slot for process %p (Size=%u MinimumBase=%p)"),
                  Process,
                  AlignedSize,
                  MinimumBase);
            return 0;
        }

        PROCESS_ARENA(Process, PROCESS_ARENA_STACK)->NextHigh = ProcessArenaAlignDown(Result);
        return Result;
    }

    return 0;
}

/************************************************************************/
