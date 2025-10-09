
/************************************************************************\

    EXOS Kernel
    Copyright (c) 1999-2025 Jango73

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


    Memory

\************************************************************************/

#include "Memory.h"

#include "Base.h"
#include "Console.h"
#include "Kernel.h"
#include "arch/i386/i386.h"
#include "Log.h"
#include "arch/i386/i386-Log.h"
#include "Schedule.h"
#include "System.h"

/************************************************************************\

    Virtual Address Space (32-bit)
    ┌──────────────────────────────────────────────────────────────────────────┐
    │ 0x00000000 .................................................. 0xBFFFFFFF │
    │                [User space]  (PDE 0..KernelDir-1)                        │
    ├──────────────────────────────────────────────────────────────────────────┤
    │ 0xC0000000 .................................................. 0xFFFFEFFF │
    │                [Kernel space] (PDE KernelDir .. 1022)                    │
    ├──────────────────────────────────────────────────────────────────────────┤
    │ 0xFFFFF000 .................................................. 0xFFFFFFFF │
    │                [Self-map window]                                         │
    │                0xFFFFF000 = PD_VA (Page Directory as an array of PDEs)   │
    │                0xFFC00000 = PT_BASE_VA (all Page Tables visible)         │
    └──────────────────────────────────────────────────────────────────────────┘

    Page Directory (1024 PDEs, each 4B)
    dir = (VMA >> 22)
    tab = (VMA >> 12) & 0x3FF
    ofs =  VMA & 0xFFF

                      PDE index
            ┌────────────┬────────────┬────────────┬────────────┬─────────────┐
            │     0      │     1      │   ...      │ KernelDir  │   1023      │
            ├────────────┼────────────┼────────────┼────────────┼─────────── ─┤
    points→ │  Low PT    │   PT #1    │   ...      │ Kernel PT  │  SELF-MAP   │
    to PA   │ (0..4MB)   │            │            │ (VMA_KERNEL)│ (PD itself)│
            └────────────┴────────────┴────────────┴────────────┴─────────────┘
                                                              ^
                                                              |
                                         PDE[1023] -> PD physical page (recursive)
                                                              |
                                                              v
    PD_VA = 0xFFFFF000 ----------------------------------> Page Directory (VA alias)


    All Page Tables via the recursive window:
    PT_BASE_VA = 0xFFC00000
    PT for PDE = D is at:   PT_VA(D) = 0xFFC00000 + D * 0x1000

    Examples:
    - PT of PDE 0:        0xFFC00000
    - PT of KernelDir:    0xFFC00000 + KernelDir*0x1000
    - PT of PDE 1023:     0xFFC00000 + 1023*0x1000  (not used for mappings)


    Resolution path for any VMA:
           VMA
            │
       dir = VMA>>22  ------>  PD_VA[dir] (PDE)  ------>  PT_VA(dir)[tab] (PTE)  ------>  PA + ofs

    Kernel mappings installed at init:
    - PDE[0]         -> Low PT (identity map 0..4MB)
    - PDE[KernelDir] -> Kernel PT (maps VMA_KERNEL .. VMA_KERNEL+4MB-1)
    - PDE[1023]      -> PD itself (self-map)


    Temporary mapping mechanism (MapTempPhysicalPage):
    1) Two VAs reserved dynamically (e.g., G_TempLinear1, G_TempLinear2).
    2) To map a physical frame P into G_TempLinear1:
       - Compute dir/tab of G_TempLinear1
       - Write the PTE via the PT window:
           PT_VA(dir) = PT_BASE_VA + dir*0x1000, entry [tab]
       - Execute `invlpg [G_TempLinear1]`
       - The physical frame P is now accessible via the VA G_TempLinear1

    Simplified view of the two temporary pages:

                         (reserved via AllocRegion, not present by default)
    G_TempLinear1  -\    ┌────────────────────────────────────────────┐
                    |-─> │ PTE < (Present=1, RW=1, ..., Address=P>>12)│  map/unmap to chosen PA
    G_TempLinear2  -/    └────────────────────────────────────────────┘
                                   ^
                                   │ (written through) PT_VA(dir(G_TempLinearX)) = PT_BASE_VA + dir*0x1000
                                   │
                              PD self-map (PD_VA, PT_BASE_VA)

    PDE[1023] points to the Page Directory itself.
    PD_VA = 0xFFFFF000 gives access to the current PD (as PTE-like entries).
    PT_BASE_VA = 0xFFC00000 provides a window for Page Tables:
    PT for directory index D is at PT_BASE_VA + (D * PAGE_SIZE).

    Temporary physical access is done by remapping two reserved
    linear pages (G_TempLinear1, G_TempLinear2, G_TempLinear3) on demand.

    =================================================================

    PCI BAR mapping process (example: Intel E1000 NIC)

    ┌───────────────────────────┐
    │  PCI Configuration Space  │
    │  (accessed via PCI config │
    │   reads/writes)           │
    └───────────┬───────────────┘
                │
                │ Read BAR0 (Base Address Register #0)
                ▼
    ┌────────────────────────────────┐
    │ BAR0 value = Physical address  │
    │ of device registers (MMIO)     │
    │ + resource size                │
    └───────────┬────────────────────┘
                │
                │ Map physical MMIO region into
                │ kernel virtual space
                │ (uncached for DMA safety)
                ▼
    ┌───────────────────────────┐
    │ AllocRegion(Base=0,       │
    │   Target=BAR0,            │
    │   Size=MMIO size,         │
    │   Flags=ALLOC_PAGES_COMMIT│
    │         | ALLOC_PAGES_UC) │
    └───────────┬───────────────┘
                │
                │ Returns Linear (VMA) address
                │ where the driver can access MMIO
                ▼
    ┌───────────────────────────────┐
    │ Driver reads/writes registers │
    │ via *(volatile U32*)(VMA+ofs) │
    │ Example: E1000_CTRL register  │
    └───────────────────────────────┘

    NOTES:
    - MMIO (Memory-Mapped I/O) must be UNCACHED (UC) to avoid
     stale data and incorrect ordering.
    - BARs can also point to I/O port ranges instead of MMIO.
    - PCI devices can have multiple BARs for different resources.

\************************************************************************/

// INTERNAL SELF-MAP + TEMP MAPPING ]
/// Architecture-specific constants are defined in arch/i386/i386-Memory.h.

// Uncomment below to mark BIOS memory pages "not present" in the page tables
// #define PROTECT_BIOS
#define PROTECTED_ZONE_START 0xC0000
#define PROTECTED_ZONE_END 0xFFFFF

// 3 on-demand temporary virtual pages, reserved at init.
static LINEAR G_TempLinear1 = 0;
static LINEAR G_TempLinear2 = 0;
static LINEAR G_TempLinear3 = 0;

/************************************************************************/

/**
 * @brief Configure the reserved temporary linear mapping slots.
 * @param Linear1 First temporary linear address.
 * @param Linear2 Second temporary linear address.
 * @param Linear3 Third temporary linear address.
 */
void MemorySetTemporaryLinearPages(LINEAR Linear1, LINEAR Linear2, LINEAR Linear3) {
    G_TempLinear1 = Linear1;
    G_TempLinear2 = Linear2;
    G_TempLinear3 = Linear3;
}

/************************************************************************/

static BOOL ValidatePhysicalTargetRange(PHYSICAL Base, UINT NumPages) {
    if (Base == 0 || NumPages == 0) return TRUE;

    UINT RequestedLength = NumPages << PAGE_SIZE_MUL;

#if defined(__EXOS_ARCH_I386__)
    U64 RangeBase;
    RangeBase.LO = (U32)Base;
    RangeBase.HI = 0;

    U64 RangeLength;
    RangeLength.LO = (U32)RequestedLength;
    RangeLength.HI = 0;

    PHYSICAL ClippedBase = 0;
    UINT ClippedLength = 0;

    if (ArchClipPhysicalRange(RangeBase, RangeLength, &ClippedBase, &ClippedLength) == FALSE) return FALSE;

    return (ClippedBase == Base && ClippedLength == RequestedLength);
#else
    PHYSICAL ClippedBase = 0;
    UINT ClippedLength = 0;

    if (ArchClipPhysicalRange((U64)Base, (U64)RequestedLength, &ClippedBase, &ClippedLength) == FALSE) return FALSE;

    return (ClippedBase == Base && ClippedLength == RequestedLength);
#endif
}

/************************************************************************/

/**
 * @brief Mark a physical page as used or free in the PPB.
 * @param Page Page index.
 * @param Used Non-zero to mark used.
 */
static void SetPhysicalPageMark(UINT Page, UINT Used) {
    UINT Offset = 0;
    UINT Value = 0;

    if (Page >= KernelStartup.PageCount) return;

    LockMutex(MUTEX_MEMORY, INFINITY);

    Offset = Page >> MUL_8;
    Value = (UINT)0x01 << (Page & 0x07);

    if (Used) {
        Kernel_i386.PPB[Offset] |= (U8)Value;
    } else {
        Kernel_i386.PPB[Offset] &= (U8)(~Value);
    }

    UnlockMutex(MUTEX_MEMORY);
}

/************************************************************************/

/**
 * @brief Query the usage mark of a physical page.
 * @param Page Page index.
 * @return Non-zero if page is used.
 */
/*
static U32 GetPhysicalPageMark(U32 Page) {
    U32 Offset = 0;
    U32 Value = 0;
    U32 RetVal = 0;

    if (Page >= KernelStartup.PageCount) return 0;

    LockMutex(MUTEX_MEMORY, INFINITY);

    Offset = Page >> MUL_8;
    Value = (U32)0x01 << (Page & 0x07);

    if (Kernel_i386.PPB[Offset] & Value) RetVal = 1;

    UnlockMutex(MUTEX_MEMORY);

    return RetVal;
}
*/

/************************************************************************/

/**
 * @brief Mark a range of physical pages as used or free.
 * @param FirstPage First page index.
 * @param PageCount Number of pages.
 * @param Used Non-zero to mark used.
 */
static void SetPhysicalPageRangeMark(UINT FirstPage, UINT PageCount, UINT Used) {
    DEBUG(TEXT("[SetPhysicalPageRangeMark] Enter"));

    UINT End = FirstPage + PageCount;
    if (FirstPage >= KernelStartup.PageCount) return;
    if (End > KernelStartup.PageCount) End = KernelStartup.PageCount;

    DEBUG(TEXT("[SetPhysicalPageRangeMark] Start, End : %x, %x"), FirstPage, End);

    for (UINT Page = FirstPage; Page < End; Page++) {
        UINT Byte = Page >> MUL_8;
        U8 Mask = (U8)(1u << (Page & 0x07)); /* bit within byte */
        if (Used) {
            Kernel_i386.PPB[Byte] |= Mask;
        } else {
            Kernel_i386.PPB[Byte] &= (U8)~Mask;
        }
    }
}

/************************************************************************/

/**
 * @brief Mark physical pages that are reserved or already used.
 */
static void MarkUsedPhysicalMemoryInternal(void) {
    UINT Start = 0;
    UINT End = (N_4MB) >> PAGE_SIZE_MUL;
    SetPhysicalPageRangeMark(Start, End, 1);

    // Derive total memory size and number of pages from the E820 map
    if (KernelStartup.E820_Count > 0) {
        PHYSICAL MaxAddress = 0;
        PHYSICAL MaxUsableRAM = 0;

        for (UINT i = 0; i < KernelStartup.E820_Count; i++) {
            const E820ENTRY* Entry = &KernelStartup.E820[i];
            PHYSICAL Base = 0;
            UINT Size = 0;

            DEBUG(TEXT("[MarkUsedPhysicalMemory] Entry base = %x, size = %x, type = %x"), Entry->Base, Entry->Size, Entry->Type);

            ArchClipPhysicalRange(Entry->Base, Entry->Size, &Base, &Size);

            PHYSICAL EntryEnd = Base + Size;
            if (EntryEnd > MaxAddress) {
                MaxAddress = EntryEnd;
            }

            if (Entry->Type == BIOS_E820_TYPE_USABLE) {
                if (EntryEnd > MaxUsableRAM) {
                    MaxUsableRAM = EntryEnd;
                }
            } else {
                UINT FirstPage = (UINT)(Base >> PAGE_SIZE_MUL);
                UINT PageCount = (UINT)((Size + PAGE_SIZE - 1) >> PAGE_SIZE_MUL);
                SetPhysicalPageRangeMark(FirstPage, PageCount, 1);
            }
        }

        KernelStartup.MemorySize = (U32)MaxUsableRAM;
        KernelStartup.PageCount = (KernelStartup.MemorySize + (PAGE_SIZE - 1)) >> PAGE_SIZE_MUL;

        DEBUG(TEXT("[MarkUsedPhysicalMemory] Memory size = %x"), KernelStartup.MemorySize);
    }
}

/************************************************************************/

/**
 * @brief Public wrapper to mark reserved and used physical pages.
 */
void MemoryMarkUsedPhysicalMemory(void) {
    MarkUsedPhysicalMemoryInternal();
}

/************************************************************************/

/**
 * @brief Allocate a free physical page.
 * @return Physical page number or MAX_U32 on failure.
 */
PHYSICAL AllocPhysicalPage(void) {
    UINT i = 0;
    UINT bit = 0;
    UINT page = 0;
    UINT mask = 0;
    UINT StartPage = 0;
    UINT StartByte = 0;
    UINT MaxByte = 0;
    PHYSICAL result = 0;

    // DEBUG(TEXT("[AllocPhysicalPage] Enter"));

    LockMutex(MUTEX_MEMORY, INFINITY);

    // Start from end of kernel region
    StartPage = RESERVED_LOW_MEMORY >> PAGE_SIZE_MUL;

    // Convert to PPB byte index
    StartByte = StartPage >> MUL_8; /* == ((... >> 12) >> 3) */
    MaxByte = (KernelStartup.PageCount + 7) >> MUL_8;

    /* Scan from StartByte upward */
    for (i = StartByte; i < MaxByte; i++) {
        U8 v = Kernel_i386.PPB[i];
        if (v != 0xFF) {
            page = (i << MUL_8); /* first page covered by this byte */
            for (bit = 0; bit < 8 && page < KernelStartup.PageCount; bit++, page++) {
                mask = 1u << bit;
                if ((v & mask) == 0) {
                    Kernel_i386.PPB[i] = (U8)(v | (U8)mask);
                    result = (PHYSICAL)(page << PAGE_SIZE_MUL); /* page * 4096 */
                    goto Out;
                }
            }
        }
    }

Out:
    // DEBUG(TEXT("[AllocPhysicalPage] Exit"));

    UnlockMutex(MUTEX_MEMORY);
    return result;
}

/************************************************************************/

/**
 * @brief Release a previously allocated physical page.
 * @param Page Page number to free.
 */
void FreePhysicalPage(PHYSICAL Page) {
    UINT StartPage = 0;
    UINT PageIndex = 0;

    if ((Page & (PAGE_SIZE - 1)) != 0) {
        ERROR(TEXT("[FreePhysicalPage] Physical address not page-aligned (%x)"), Page);
        return;
    }

    // Start from end of kernel region (KER + BSS + STK), in pages
    StartPage = RESERVED_LOW_MEMORY >> PAGE_SIZE_MUL;

    // Translate PA -> page index
    PageIndex = (U32)(Page >> PAGE_SIZE_MUL);

    // Guard: null and alignment
    if (PageIndex < StartPage) return;

    // Guard: never free page 0 (kept reserved on purpose)
    if (PageIndex == 0) {
        ERROR(TEXT("[FreePhysicalPage] Attempt to free page 0"));
        return;
    }

    // Bounds check
    if (PageIndex >= KernelStartup.PageCount) {
        ERROR(TEXT("[FreePhysicalPage] Page index out of range (%x)"), PageIndex);
        return;
    }

    LockMutex(MUTEX_MEMORY, INFINITY);

    // Bitmap math: 8 pages per byte
    UINT ByteIndex = PageIndex >> MUL_8;        // == PageIndex / 8
    U8 mask = (U8)(1u << (PageIndex & 0x07));  // bit within the byte

    // If already free, nothing to do
    if ((Kernel_i386.PPB[ByteIndex] & mask) == 0) {
        UnlockMutex(MUTEX_MEMORY);
        DEBUG(TEXT("[FreePhysicalPage] Page already free (PA=%x)"), Page);
        return;
    }

    // Mark page as free
    Kernel_i386.PPB[ByteIndex] = (U8)(Kernel_i386.PPB[ByteIndex] & (U8)~mask);

    UnlockMutex(MUTEX_MEMORY);
}

/************************************************************************/
// Paging helpers are provided by arch/i386/i386-Memory.h.
/************************************************************************/

/************************************************************************/
// Map or remap a single virtual page by directly editing its PTE via the self-map.

static inline void MapOnePage(
    LINEAR Linear, PHYSICAL Physical, U32 ReadWrite, U32 Privilege, U32 WriteThrough, U32 CacheDisabled, U32 Global,
    U32 Fixed) {
    LPPAGE_DIRECTORY Directory = GetCurrentPageDirectoryVA();
    UINT dir = GetDirectoryEntry(Linear);

    if (!PageDirectoryEntryIsPresent(Directory, dir)) {
        ERROR(TEXT("[MapOnePage] PDE not present for VA %x (dir=%d)"), Linear, dir);
        return;  // Or panic
    }

    LPPAGE_TABLE Table = GetPageTableVAFor(Linear);
    UINT tab = GetTableEntry(Linear);

    WritePageTableEntryValue(
        Table, tab, MakePageTableEntryValue(Physical, ReadWrite, Privilege, WriteThrough, CacheDisabled, Global, Fixed));
    InvalidatePage(Linear);
}

/************************************************************************/

/**
 * @brief Unmap a single page from the current address space.
 * @param Linear Linear address to unmap.
 */
static inline void UnmapOnePage(LINEAR Linear) {
    LPPAGE_TABLE Table = GetPageTableVAFor(Linear);
    UINT tab = GetTableEntry(Linear);
    ClearPageTableEntry(Table, tab);
    InvalidatePage(Linear);
}

/************************************************************************/

/**
 * @brief Check if a linear address is mapped and accessible.
 * @param Pointer Linear address to test.
 * @return TRUE if address is valid.
 */
BOOL IsValidMemory(LINEAR Pointer) {
    LPPAGE_DIRECTORY Directory = GetCurrentPageDirectoryVA();

    UINT dir = GetDirectoryEntry(Pointer);
    UINT tab = GetTableEntry(Pointer);

    // Bounds check
    if (dir >= PAGE_TABLE_NUM_ENTRIES) return FALSE;
    if (tab >= PAGE_TABLE_NUM_ENTRIES) return FALSE;

    // Page directory present?
    if (!PageDirectoryEntryIsPresent(Directory, dir)) return FALSE;

    // Page table present?
    LPPAGE_TABLE Table = GetPageTableVAFor(Pointer);
    if (!PageTableEntryIsPresent(Table, tab)) return FALSE;

    return TRUE;
}

/************************************************************************/
// Public temporary map #1

/**
 * @brief Map a physical page to a temporary linear address.
 * @param Physical Physical page number.
 * @return Linear address mapping or 0 on failure.
 */
LINEAR MapTempPhysicalPage(PHYSICAL Physical) {
    if (G_TempLinear1 == 0) {
        ERROR(TEXT("[MapTempPhysicalPage] Temp slot #1 not reserved"));
        return NULL;
    }
    MapOnePage(
        G_TempLinear1, Physical,
        /*RW*/ 1, PAGE_PRIVILEGE_KERNEL, /*WT*/ 0, /*UC*/ 0, /*Global*/ 0, /*Fixed*/ 1);
    return G_TempLinear1;
}

/************************************************************************/
// Public temporary map #2

/**
 * @brief Map a physical page to the second temporary linear address.
 * @param Physical Physical page number.
 * @return Linear address mapping or 0 on failure.
 */
LINEAR MapTempPhysicalPage2(PHYSICAL Physical) {
    if (G_TempLinear2 == 0) {
        ERROR(TEXT("[MapTempPhysicalPage2] Temp slot #2 not reserved"));
        return NULL;
    }
    MapOnePage(
        G_TempLinear2, Physical,
        /*RW*/ 1, PAGE_PRIVILEGE_KERNEL, /*WT*/ 0, /*UC*/ 0, /*Global*/ 0, /*Fixed*/ 1);
    return G_TempLinear2;
}

/************************************************************************/
// Public temporary map #3

/**
 * @brief Map a physical page to the third temporary linear address.
 * @param Physical Physical page number.
 * @return Linear address mapping or 0 on failure.
 */
LINEAR MapTempPhysicalPage3(PHYSICAL Physical) {
    if (G_TempLinear3 == 0) {
        ERROR(TEXT("[MapTempPhysicalPage3] Temp slot #3 not reserved"));
        return NULL;
    }
    MapOnePage(
        G_TempLinear3, Physical,
        /*RW*/ 1, PAGE_PRIVILEGE_KERNEL, /*WT*/ 0, /*UC*/ 0, /*Global*/ 0, /*Fixed*/ 1);
    return G_TempLinear3;
}

/************************************************************************/

/**
 * @brief Allocate a new page directory.
 * @return Physical address of the page directory or MAX_U32 on failure.
 */
#if defined(__EXOS_ARCH_X86_64__)
PHYSICAL AllocPageDirectory(void) {
    PHYSICAL Pml4Physical = NULL;
    PHYSICAL LowPdptPhysical = NULL;
    PHYSICAL KernelPdptPhysical = NULL;
    PHYSICAL TaskRunnerPdptPhysical = NULL;
    PHYSICAL LowDirectoryPhysical = NULL;
    PHYSICAL KernelDirectoryPhysical = NULL;
    PHYSICAL TaskRunnerDirectoryPhysical = NULL;
    PHYSICAL PMA_LowTable = NULL;
    PHYSICAL PMA_KernelTable = NULL;
    PHYSICAL PMA_TaskRunnerTable = NULL;

    DEBUG(TEXT("[AllocPageDirectory] Enter"));

    PHYSICAL PhysBaseKernel = KernelStartup.StubAddress;

    UINT LowPml4Index = GetPml4Entry(0ull);
    UINT LowPdptIndex = GetPdptEntry(0ull);
    UINT LowDirectoryIndex = GetDirectoryEntry(0ull);

    UINT KernelPml4Index = GetPml4Entry((U64)VMA_KERNEL);
    UINT KernelPdptIndex = GetPdptEntry((U64)VMA_KERNEL);
    UINT KernelDirectoryIndex = GetDirectoryEntry((U64)VMA_KERNEL);

    UINT TaskRunnerPml4Index = GetPml4Entry((U64)VMA_TASK_RUNNER);
    UINT TaskRunnerPdptIndex = GetPdptEntry((U64)VMA_TASK_RUNNER);
    UINT TaskRunnerDirectoryIndex = GetDirectoryEntry((U64)VMA_TASK_RUNNER);
    UINT TaskRunnerTableIndex = GetTableEntry((U64)VMA_TASK_RUNNER);

    Pml4Physical = AllocPhysicalPage();
    LowPdptPhysical = AllocPhysicalPage();
    KernelPdptPhysical = AllocPhysicalPage();
    TaskRunnerPdptPhysical = AllocPhysicalPage();
    LowDirectoryPhysical = AllocPhysicalPage();
    KernelDirectoryPhysical = AllocPhysicalPage();
    TaskRunnerDirectoryPhysical = AllocPhysicalPage();
    PMA_LowTable = AllocPhysicalPage();
    PMA_KernelTable = AllocPhysicalPage();
    PMA_TaskRunnerTable = AllocPhysicalPage();

    if (Pml4Physical == NULL || LowPdptPhysical == NULL || KernelPdptPhysical == NULL ||
        TaskRunnerPdptPhysical == NULL || LowDirectoryPhysical == NULL || KernelDirectoryPhysical == NULL ||
        TaskRunnerDirectoryPhysical == NULL || PMA_LowTable == NULL || PMA_KernelTable == NULL ||
        PMA_TaskRunnerTable == NULL) {
        ERROR(TEXT("[AllocPageDirectory] Out of physical pages"));
        goto Out_Error64;
    }

    LINEAR VMA_LowPdpt = MapTempPhysicalPage(LowPdptPhysical);
    if (VMA_LowPdpt == NULL) {
        ERROR(TEXT("[AllocPageDirectory] MapTempPhysicalPage failed on LowPdpt"));
        goto Out_Error64;
    }
    LPPAGE_DIRECTORY LowPdpt = (LPPAGE_DIRECTORY)VMA_LowPdpt;
    MemorySet(LowPdpt, 0, PAGE_SIZE);
    DEBUG(TEXT("[AllocPageDirectory] Low PDPT cleared"));

    LINEAR VMA_LowDirectory = MapTempPhysicalPage2(LowDirectoryPhysical);
    if (VMA_LowDirectory == NULL) {
        ERROR(TEXT("[AllocPageDirectory] MapTempPhysicalPage2 failed on LowDirectory"));
        goto Out_Error64;
    }
    LPPAGE_DIRECTORY LowDirectory = (LPPAGE_DIRECTORY)VMA_LowDirectory;
    MemorySet(LowDirectory, 0, PAGE_SIZE);
    DEBUG(TEXT("[AllocPageDirectory] Low directory cleared"));

    LINEAR VMA_LowTable = MapTempPhysicalPage3(PMA_LowTable);
    if (VMA_LowTable == NULL) {
        ERROR(TEXT("[AllocPageDirectory] MapTempPhysicalPage3 failed on LowTable"));
        goto Out_Error64;
    }
    LPPAGE_TABLE LowTable = (LPPAGE_TABLE)VMA_LowTable;
    MemorySet(LowTable, 0, PAGE_SIZE);
    DEBUG(TEXT("[AllocPageDirectory] Low table cleared"));

    for (UINT Index = 0; Index < PAGE_TABLE_NUM_ENTRIES; Index++) {
        PHYSICAL Physical = (PHYSICAL)Index << PAGE_SIZE_MUL;

#ifdef PROTECT_BIOS
        BOOL Protected = Physical == 0 || (Physical > PROTECTED_ZONE_START && Physical <= PROTECTED_ZONE_END);
#else
        BOOL Protected = FALSE;
#endif

        if (Protected) {
            ClearPageTableEntry(LowTable, Index);
        } else {
            WritePageTableEntryValue(
                LowTable,
                Index,
                MakePageTableEntryValue(
                    Physical,
                    /*ReadWrite*/ 1,
                    PAGE_PRIVILEGE_KERNEL,
                    /*WriteThrough*/ 0,
                    /*CacheDisabled*/ 0,
                    /*Global*/ 0,
                    /*Fixed*/ 1));
        }
    }

    WritePageDirectoryEntryValue(
        LowDirectory,
        LowDirectoryIndex,
        MakePageDirectoryEntryValue(
            PMA_LowTable,
            /*ReadWrite*/ 1,
            PAGE_PRIVILEGE_KERNEL,
            /*WriteThrough*/ 0,
            /*CacheDisabled*/ 0,
            /*Global*/ 0,
            /*Fixed*/ 1));

    WritePageDirectoryEntryValue(
        LowPdpt,
        LowPdptIndex,
        MakePageDirectoryEntryValue(
            LowDirectoryPhysical,
            /*ReadWrite*/ 1,
            PAGE_PRIVILEGE_KERNEL,
            /*WriteThrough*/ 0,
            /*CacheDisabled*/ 0,
            /*Global*/ 0,
            /*Fixed*/ 1));

    LINEAR VMA_KernelPdpt = MapTempPhysicalPage(KernelPdptPhysical);
    if (VMA_KernelPdpt == NULL) {
        ERROR(TEXT("[AllocPageDirectory] MapTempPhysicalPage failed on KernelPdpt"));
        goto Out_Error64;
    }
    LPPAGE_DIRECTORY KernelPdpt = (LPPAGE_DIRECTORY)VMA_KernelPdpt;
    MemorySet(KernelPdpt, 0, PAGE_SIZE);
    DEBUG(TEXT("[AllocPageDirectory] Kernel PDPT cleared"));

    LINEAR VMA_KernelDirectory = MapTempPhysicalPage2(KernelDirectoryPhysical);
    if (VMA_KernelDirectory == NULL) {
        ERROR(TEXT("[AllocPageDirectory] MapTempPhysicalPage2 failed on KernelDirectory"));
        goto Out_Error64;
    }
    LPPAGE_DIRECTORY KernelDirectory = (LPPAGE_DIRECTORY)VMA_KernelDirectory;
    MemorySet(KernelDirectory, 0, PAGE_SIZE);
    DEBUG(TEXT("[AllocPageDirectory] Kernel directory cleared"));

    LINEAR VMA_KernelTable = MapTempPhysicalPage3(PMA_KernelTable);
    if (VMA_KernelTable == NULL) {
        ERROR(TEXT("[AllocPageDirectory] MapTempPhysicalPage3 failed on KernelTable"));
        goto Out_Error64;
    }
    LPPAGE_TABLE KernelTable = (LPPAGE_TABLE)VMA_KernelTable;
    MemorySet(KernelTable, 0, PAGE_SIZE);
    DEBUG(TEXT("[AllocPageDirectory] Kernel table cleared"));

    for (UINT Index = 0; Index < PAGE_TABLE_NUM_ENTRIES; Index++) {
        PHYSICAL Physical = PhysBaseKernel + ((PHYSICAL)Index << PAGE_SIZE_MUL);
        WritePageTableEntryValue(
            KernelTable,
            Index,
            MakePageTableEntryValue(
                Physical,
                /*ReadWrite*/ 1,
                PAGE_PRIVILEGE_KERNEL,
                /*WriteThrough*/ 0,
                /*CacheDisabled*/ 0,
                /*Global*/ 0,
                /*Fixed*/ 1));
    }

    WritePageDirectoryEntryValue(
        KernelDirectory,
        KernelDirectoryIndex,
        MakePageDirectoryEntryValue(
            PMA_KernelTable,
            /*ReadWrite*/ 1,
            PAGE_PRIVILEGE_KERNEL,
            /*WriteThrough*/ 0,
            /*CacheDisabled*/ 0,
            /*Global*/ 0,
            /*Fixed*/ 1));

    WritePageDirectoryEntryValue(
        KernelPdpt,
        KernelPdptIndex,
        MakePageDirectoryEntryValue(
            KernelDirectoryPhysical,
            /*ReadWrite*/ 1,
            PAGE_PRIVILEGE_KERNEL,
            /*WriteThrough*/ 0,
            /*CacheDisabled*/ 0,
            /*Global*/ 0,
            /*Fixed*/ 1));

    LINEAR VMA_TaskRunnerPdpt = MapTempPhysicalPage(TaskRunnerPdptPhysical);
    if (VMA_TaskRunnerPdpt == NULL) {
        ERROR(TEXT("[AllocPageDirectory] MapTempPhysicalPage failed on TaskRunnerPdpt"));
        goto Out_Error64;
    }
    LPPAGE_DIRECTORY TaskRunnerPdpt = (LPPAGE_DIRECTORY)VMA_TaskRunnerPdpt;
    MemorySet(TaskRunnerPdpt, 0, PAGE_SIZE);
    DEBUG(TEXT("[AllocPageDirectory] TaskRunner PDPT cleared"));

    LINEAR VMA_TaskRunnerDirectory = MapTempPhysicalPage2(TaskRunnerDirectoryPhysical);
    if (VMA_TaskRunnerDirectory == NULL) {
        ERROR(TEXT("[AllocPageDirectory] MapTempPhysicalPage2 failed on TaskRunnerDirectory"));
        goto Out_Error64;
    }
    LPPAGE_DIRECTORY TaskRunnerDirectory = (LPPAGE_DIRECTORY)VMA_TaskRunnerDirectory;
    MemorySet(TaskRunnerDirectory, 0, PAGE_SIZE);
    DEBUG(TEXT("[AllocPageDirectory] TaskRunner directory cleared"));

    LINEAR VMA_TaskRunnerTable = MapTempPhysicalPage3(PMA_TaskRunnerTable);
    if (VMA_TaskRunnerTable == NULL) {
        ERROR(TEXT("[AllocPageDirectory] MapTempPhysicalPage3 failed on TaskRunnerTable"));
        goto Out_Error64;
    }
    LPPAGE_TABLE TaskRunnerTable = (LPPAGE_TABLE)VMA_TaskRunnerTable;
    MemorySet(TaskRunnerTable, 0, PAGE_SIZE);
    DEBUG(TEXT("[AllocPageDirectory] TaskRunner table cleared"));

    U64 TaskRunnerLinear = (U64)(unsigned long long)&__task_runner_start;
    PHYSICAL TaskRunnerPhysical = PhysBaseKernel + (PHYSICAL)(TaskRunnerLinear - (U64)VMA_KERNEL);

    DEBUG(TEXT("[AllocPageDirectory] TaskRunnerPhysical = %x + (%llx - %llx) = %x"),
        (UINT)PhysBaseKernel,
        (unsigned long long)TaskRunnerLinear,
        (unsigned long long)VMA_KERNEL,
        (UINT)TaskRunnerPhysical);

    WritePageTableEntryValue(
        TaskRunnerTable,
        TaskRunnerTableIndex,
        MakePageTableEntryValue(
            TaskRunnerPhysical,
            /*ReadWrite*/ 0,
            PAGE_PRIVILEGE_USER,
            /*WriteThrough*/ 0,
            /*CacheDisabled*/ 0,
            /*Global*/ 0,
            /*Fixed*/ 1));

    WritePageDirectoryEntryValue(
        TaskRunnerDirectory,
        TaskRunnerDirectoryIndex,
        MakePageDirectoryEntryValue(
            PMA_TaskRunnerTable,
            /*ReadWrite*/ 1,
            PAGE_PRIVILEGE_USER,
            /*WriteThrough*/ 0,
            /*CacheDisabled*/ 0,
            /*Global*/ 0,
            /*Fixed*/ 1));

    WritePageDirectoryEntryValue(
        TaskRunnerPdpt,
        TaskRunnerPdptIndex,
        MakePageDirectoryEntryValue(
            TaskRunnerDirectoryPhysical,
            /*ReadWrite*/ 1,
            PAGE_PRIVILEGE_USER,
            /*WriteThrough*/ 0,
            /*CacheDisabled*/ 0,
            /*Global*/ 0,
            /*Fixed*/ 1));

    LINEAR VMA_Pml4 = MapTempPhysicalPage(Pml4Physical);
    if (VMA_Pml4 == NULL) {
        ERROR(TEXT("[AllocPageDirectory] MapTempPhysicalPage failed on PML4"));
        goto Out_Error64;
    }
    LPPAGE_DIRECTORY Pml4 = (LPPAGE_DIRECTORY)VMA_Pml4;
    MemorySet(Pml4, 0, PAGE_SIZE);
    DEBUG(TEXT("[AllocPageDirectory] PML4 cleared"));

    WritePageDirectoryEntryValue(
        Pml4,
        LowPml4Index,
        MakePageDirectoryEntryValue(
            LowPdptPhysical,
            /*ReadWrite*/ 1,
            PAGE_PRIVILEGE_KERNEL,
            /*WriteThrough*/ 0,
            /*CacheDisabled*/ 0,
            /*Global*/ 0,
            /*Fixed*/ 1));

    WritePageDirectoryEntryValue(
        Pml4,
        KernelPml4Index,
        MakePageDirectoryEntryValue(
            KernelPdptPhysical,
            /*ReadWrite*/ 1,
            PAGE_PRIVILEGE_KERNEL,
            /*WriteThrough*/ 0,
            /*CacheDisabled*/ 0,
            /*Global*/ 0,
            /*Fixed*/ 1));

    WritePageDirectoryEntryValue(
        Pml4,
        TaskRunnerPml4Index,
        MakePageDirectoryEntryValue(
            TaskRunnerPdptPhysical,
            /*ReadWrite*/ 1,
            PAGE_PRIVILEGE_USER,
            /*WriteThrough*/ 0,
            /*CacheDisabled*/ 0,
            /*Global*/ 0,
            /*Fixed*/ 1));

    WritePageDirectoryEntryValue(
        Pml4,
        PML4_RECURSIVE_SLOT,
        MakePageDirectoryEntryValue(
            Pml4Physical,
            /*ReadWrite*/ 1,
            PAGE_PRIVILEGE_KERNEL,
            /*WriteThrough*/ 0,
            /*CacheDisabled*/ 0,
            /*Global*/ 0,
            /*Fixed*/ 1));

    FlushTLB();

    DEBUG(TEXT("[AllocPageDirectory] PML4[%u]=%llx, PML4[%u]=%llx, PML4[%u]=%llx, PML4[%u]=%llx"),
        LowPml4Index,
        (unsigned long long)ReadPageDirectoryEntryValue(Pml4, LowPml4Index),
        KernelPml4Index,
        (unsigned long long)ReadPageDirectoryEntryValue(Pml4, KernelPml4Index),
        TaskRunnerPml4Index,
        (unsigned long long)ReadPageDirectoryEntryValue(Pml4, TaskRunnerPml4Index),
        PML4_RECURSIVE_SLOT,
        (unsigned long long)ReadPageDirectoryEntryValue(Pml4, PML4_RECURSIVE_SLOT));

    DEBUG(TEXT("[AllocPageDirectory] LowTable[0]=%llx, KernelTable[0]=%llx, TaskRunnerTable[%u]=%llx"),
        (unsigned long long)ReadPageTableEntryValue(LowTable, 0),
        (unsigned long long)ReadPageTableEntryValue(KernelTable, 0),
        TaskRunnerTableIndex,
        (unsigned long long)ReadPageTableEntryValue(TaskRunnerTable, TaskRunnerTableIndex));

    DEBUG(TEXT("[AllocPageDirectory] TaskRunner VMA=%llx -> Physical=%x"),
        (unsigned long long)VMA_TASK_RUNNER,
        (UINT)TaskRunnerPhysical);

    DEBUG(TEXT("[AllocPageDirectory] Exit"));
    return Pml4Physical;

Out_Error64:

    if (Pml4Physical) FreePhysicalPage(Pml4Physical);
    if (LowPdptPhysical) FreePhysicalPage(LowPdptPhysical);
    if (KernelPdptPhysical) FreePhysicalPage(KernelPdptPhysical);
    if (TaskRunnerPdptPhysical) FreePhysicalPage(TaskRunnerPdptPhysical);
    if (LowDirectoryPhysical) FreePhysicalPage(LowDirectoryPhysical);
    if (KernelDirectoryPhysical) FreePhysicalPage(KernelDirectoryPhysical);
    if (TaskRunnerDirectoryPhysical) FreePhysicalPage(TaskRunnerDirectoryPhysical);
    if (PMA_LowTable) FreePhysicalPage(PMA_LowTable);
    if (PMA_KernelTable) FreePhysicalPage(PMA_KernelTable);
    if (PMA_TaskRunnerTable) FreePhysicalPage(PMA_TaskRunnerTable);

    return NULL;
}
#else
PHYSICAL AllocPageDirectory(void) {
    PHYSICAL PMA_Directory = NULL;
    PHYSICAL PMA_LowTable = NULL;
    PHYSICAL PMA_KernelTable = NULL;
    PHYSICAL PMA_TaskRunnerTable = NULL;

    LPPAGE_DIRECTORY Directory = NULL;
    LPPAGE_TABLE LowTable = NULL;
    LPPAGE_TABLE KernelTable = NULL;
    LPPAGE_TABLE TaskRunnerTable = NULL;

    DEBUG(TEXT("[AllocPageDirectory] Enter"));

    UINT DirKernel = (VMA_KERNEL >> PAGE_TABLE_CAPACITY_MUL);           // 4MB directory slot for VMA_KERNEL
    UINT DirTaskRunner = (VMA_TASK_RUNNER >> PAGE_TABLE_CAPACITY_MUL);  // 4MB directory slot for VMA_TASK_RUNNER
    PHYSICAL PhysBaseKernel = KernelStartup.StubAddress;                // Kernel physical base
    UINT Index;

    // Allocate required physical pages (PD + 3 PTs)
    PMA_Directory = AllocPhysicalPage();
    PMA_LowTable = AllocPhysicalPage();
    PMA_KernelTable = AllocPhysicalPage();
    PMA_TaskRunnerTable = AllocPhysicalPage();

    if (PMA_Directory == NULL || PMA_LowTable == NULL || PMA_KernelTable == NULL || PMA_TaskRunnerTable == NULL) {
        ERROR(TEXT("[AllocPageDirectory] Out of physical pages"));
        goto Out_Error;
    }

    // Clear and prepare the Page Directory
    LINEAR VMA_PD = MapTempPhysicalPage(PMA_Directory);
    if (VMA_PD == NULL) {
        ERROR(TEXT("[AllocPageDirectory] MapTempPhysicalPage failed on Directory"));
        goto Out_Error;
    }
    Directory = (LPPAGE_DIRECTORY)VMA_PD;
    MemorySet(Directory, 0, PAGE_SIZE);

    DEBUG(TEXT("[AllocPageDirectory] Page directory cleared"));

    // Directory[0] -> identity map 0..4MB via PMA_LowTable
    WritePageDirectoryEntryValue(
        Directory,
        0,
        MakePageDirectoryEntryValue(
            PMA_LowTable,
            /*ReadWrite*/ 1,
            PAGE_PRIVILEGE_KERNEL,
            /*WriteThrough*/ 0,
            /*CacheDisabled*/ 0,
            /*Global*/ 0,
            /*Fixed*/ 1));

    // Directory[DirKernel] -> map VMA_KERNEL..VMA_KERNEL+4MB-1 to KERNEL_PHYSICAL_ORIGIN..+4MB-1
    WritePageDirectoryEntryValue(
        Directory,
        DirKernel,
        MakePageDirectoryEntryValue(
            PMA_KernelTable,
            /*ReadWrite*/ 1,
            PAGE_PRIVILEGE_KERNEL,
            /*WriteThrough*/ 0,
            /*CacheDisabled*/ 0,
            /*Global*/ 0,
            /*Fixed*/ 1));

    // Directory[DirTaskRunner] -> map VMA_TASK_RUNNER (one page) to TaskRunner physical location
    WritePageDirectoryEntryValue(
        Directory,
        DirTaskRunner,
        MakePageDirectoryEntryValue(
            PMA_TaskRunnerTable,
            /*ReadWrite*/ 1,
            PAGE_PRIVILEGE_USER,
            /*WriteThrough*/ 0,
            /*CacheDisabled*/ 0,
            /*Global*/ 0,
            /*Fixed*/ 1));

    // Install recursive mapping: PDE[1023] = PD
    WritePageDirectoryEntryValue(
        Directory,
        PD_RECURSIVE_SLOT,
        MakePageDirectoryEntryValue(
            PMA_Directory,
            /*ReadWrite*/ 1,
            PAGE_PRIVILEGE_KERNEL,
            /*WriteThrough*/ 0,
            /*CacheDisabled*/ 0,
            /*Global*/ 0,
            /*Fixed*/ 1));

    // Fill identity-mapped low table (0..4MB)
    LINEAR VMA_PT = MapTempPhysicalPage2(PMA_LowTable);
    if (VMA_PT == NULL) {
        ERROR(TEXT("[AllocPageDirectory] MapTempPhysicalPage2 failed on LowTable"));
        goto Out_Error;
    }
    LowTable = (LPPAGE_TABLE)VMA_PT;
    MemorySet(LowTable, 0, PAGE_SIZE);

    DEBUG(TEXT("[AllocPageDirectory] Low memory table cleared"));

    for (Index = 0; Index < PAGE_TABLE_NUM_ENTRIES; Index++) {
        PHYSICAL Physical = (PHYSICAL)Index << PAGE_SIZE_MUL;

        #ifdef PROTECT_BIOS
        BOOL Protected = Physical == 0 || (Physical > PROTECTED_ZONE_START && Physical <= PROTECTED_ZONE_END);
        #else
        BOOL Protected = FALSE;
        #endif

        if (Protected) {
            ClearPageTableEntry(LowTable, Index);
        } else {
            WritePageTableEntryValue(
                LowTable,
                Index,
                MakePageTableEntryValue(
                    Physical,
                    /*ReadWrite*/ 1,
                    PAGE_PRIVILEGE_KERNEL,
                    /*WriteThrough*/ 0,
                    /*CacheDisabled*/ 0,
                    /*Global*/ 0,
                    /*Fixed*/ 1));
        }
    }

    // Fill kernel mapping table by copying the current kernel PT
    VMA_PT = MapTempPhysicalPage2(PMA_KernelTable);
    if (VMA_PT == NULL) {
        ERROR(TEXT("[AllocPageDirectory] MapTempPhysicalPage2 failed on KernelTable"));
        goto Out_Error;
    }
    KernelTable = (LPPAGE_TABLE)VMA_PT;

    MemorySet(KernelTable, 0, PAGE_SIZE);

    DEBUG(TEXT("[AllocPageDirectory] Kernel table cleared"));

    for (Index = 0; Index < PAGE_TABLE_NUM_ENTRIES; Index++) {
        PHYSICAL Physical = PhysBaseKernel + ((PHYSICAL)Index << PAGE_SIZE_MUL);
        WritePageTableEntryValue(
            KernelTable,
            Index,
            MakePageTableEntryValue(
                Physical,
                /*ReadWrite*/ 1,
                PAGE_PRIVILEGE_KERNEL,
                /*WriteThrough*/ 0,
                /*CacheDisabled*/ 0,
                /*Global*/ 0,
                /*Fixed*/ 1));
    }

    // Fill TaskRunner page table - only map the first page where TaskRunner is located
    VMA_PT = MapTempPhysicalPage2(PMA_TaskRunnerTable);
    if (VMA_PT == NULL) {
        ERROR(TEXT("[AllocPageDirectory] MapTempPhysicalPage2 failed on TaskRunnerTable"));
        goto Out_Error;
    }
    TaskRunnerTable = (LPPAGE_TABLE)VMA_PT;
    MemorySet(TaskRunnerTable, 0, PAGE_SIZE);

    DEBUG(TEXT("[AllocPageDirectory] TaskRunner table cleared"));

    LINEAR TaskRunnerLinear = (LINEAR)&__task_runner_start;
    PHYSICAL TaskRunnerPhysical = PhysBaseKernel + (PHYSICAL)(TaskRunnerLinear - VMA_KERNEL);

    DEBUG(TEXT("[AllocPageDirectory] TaskRunnerPhysical = %x + (%x - %x) = %x"),
        (UINT)PhysBaseKernel, (UINT)TaskRunnerLinear, (UINT)VMA_KERNEL, (UINT)TaskRunnerPhysical);

    UINT TaskRunnerTableIndex = GetTableEntry(VMA_TASK_RUNNER);

    WritePageTableEntryValue(
        TaskRunnerTable,
        TaskRunnerTableIndex,
        MakePageTableEntryValue(
            TaskRunnerPhysical,
            /*ReadWrite*/ 0,  // Read-only for user
            PAGE_PRIVILEGE_USER,
            /*WriteThrough*/ 0,
            /*CacheDisabled*/ 0,
            /*Global*/ 0,
            /*Fixed*/ 1));

    // TLB sync before returning
    FlushTLB();

    DEBUG(TEXT("[AllocPageDirectory] PDE[0]=%x, PDE[768]=%x, PDE[%u]=%x, PDE[1023]=%x"),
        ReadPageDirectoryEntryValue(Directory, 0), ReadPageDirectoryEntryValue(Directory, 768), DirTaskRunner,
        ReadPageDirectoryEntryValue(Directory, DirTaskRunner), ReadPageDirectoryEntryValue(Directory, 1023));
    DEBUG(TEXT("[AllocPageDirectory] LowTable[0]=%x, KernelTable[0]=%x, TaskRunnerTable[%u]=%x"),
        ReadPageTableEntryValue(LowTable, 0), ReadPageTableEntryValue(KernelTable, 0), TaskRunnerTableIndex,
        ReadPageTableEntryValue(TaskRunnerTable, TaskRunnerTableIndex));
    DEBUG(TEXT("[AllocPageDirectory] TaskRunner VMA=%x -> Physical=%x"), VMA_TASK_RUNNER, TaskRunnerPhysical);

    DEBUG(TEXT("[AllocPageDirectory] Exit"));
    return PMA_Directory;

Out_Error:

    if (PMA_Directory) FreePhysicalPage(PMA_Directory);
    if (PMA_LowTable) FreePhysicalPage(PMA_LowTable);
    if (PMA_KernelTable) FreePhysicalPage(PMA_KernelTable);
    if (PMA_TaskRunnerTable) FreePhysicalPage(PMA_TaskRunnerTable);

    return NULL;
}

#endif

/************************************************************************/

/**
 * @brief Allocate a new page directory for userland processes.
 * @return Physical address of the page directory or NULL on failure.
 */
PHYSICAL AllocUserPageDirectory(void) {
    PHYSICAL PMA_Directory = NULL;
    PHYSICAL PMA_LowTable = NULL;
    PHYSICAL PMA_KernelTable = NULL;

    LPPAGE_DIRECTORY Directory = NULL;
    LPPAGE_TABLE LowTable = NULL;
    LPPAGE_TABLE KernelTable = NULL;
    LPPAGE_DIRECTORY CurrentPD = (LPPAGE_DIRECTORY)PD_VA;

    DEBUG(TEXT("[AllocUserPageDirectory] Enter"));

    UINT DirKernel = (VMA_KERNEL >> PAGE_TABLE_CAPACITY_MUL);  // 4MB directory slot for VMA_KERNEL
    PHYSICAL PhysBaseKernel = KernelStartup.StubAddress;       // Kernel physical base
    UINT Index;

    // Allocate required physical pages (PD + 4 PTs)
    PMA_Directory = AllocPhysicalPage();
    PMA_LowTable = AllocPhysicalPage();
    PMA_KernelTable = AllocPhysicalPage();

    if (PMA_Directory == NULL || PMA_LowTable == NULL || PMA_KernelTable == NULL) {
        ERROR(TEXT("[AllocUserPageDirectory] Out of physical pages"));
        goto Out_Error;
    }

    // Clear and prepare the Page Directory
    LINEAR VMA_PD = MapTempPhysicalPage(PMA_Directory);
    if (VMA_PD == NULL) {
        ERROR(TEXT("[AllocUserPageDirectory] MapTempPhysicalPage failed on Directory"));
        goto Out_Error;
    }
    Directory = (LPPAGE_DIRECTORY)VMA_PD;
    MemorySet(Directory, 0, PAGE_SIZE);

    DEBUG(TEXT("[AllocUserPageDirectory] Page directory cleared"));

    // Directory[0] -> identity map 0..4MB via PMA_LowTable
    WritePageDirectoryEntryValue(
        Directory,
        0,
        MakePageDirectoryEntryValue(
            PMA_LowTable,
            /*ReadWrite*/ 1,
            PAGE_PRIVILEGE_KERNEL,
            /*WriteThrough*/ 0,
            /*CacheDisabled*/ 0,
            /*Global*/ 0,
            /*Fixed*/ 1));

    // Directory[DirKernel] -> map VMA_KERNEL..VMA_KERNEL+4MB-1 to current kernel state
    WritePageDirectoryEntryValue(
        Directory,
        DirKernel,
        MakePageDirectoryEntryValue(
            PMA_KernelTable,
            /*ReadWrite*/ 1,
            PAGE_PRIVILEGE_KERNEL,
            /*WriteThrough*/ 0,
            /*CacheDisabled*/ 0,
            /*Global*/ 0,
            /*Fixed*/ 1));

    // Copy present PDEs from current directory, but skip user space (VMA_USER to VMA_LIBRARY-1)
    // to allow new process to allocate its own region at VMA_USER
    UNUSED(VMA_TASK_RUNNER);
    UINT UserStartPDE = GetDirectoryEntry(VMA_USER);             // PDE index for VMA_USER
    UINT UserEndPDE = GetDirectoryEntry(VMA_LIBRARY - 1) - 1;    // PDE index for VMA_LIBRARY-1, excluding TaskRunner space
    for (Index = 1; Index < 1023; Index++) {                            // Skip 0 (already done) and 1023 (self-map)
        if (PageDirectoryEntryIsPresent(CurrentPD, Index) && Index != DirKernel) {
            // Skip user space PDEs to avoid copying current process's user space
            if (Index >= UserStartPDE && Index <= UserEndPDE) {
                DEBUG(TEXT("[AllocUserPageDirectory] Skipped user space PDE[%u]"), Index);
                continue;
            }
            WritePageDirectoryEntryValue(Directory, Index, ReadPageDirectoryEntryValue(CurrentPD, Index));
            DEBUG(TEXT("[AllocUserPageDirectory] Copied PDE[%u]"), Index);
        }
    }

    // Install recursive mapping: PDE[1023] = PD
    WritePageDirectoryEntryValue(
        Directory,
        PD_RECURSIVE_SLOT,
        MakePageDirectoryEntryValue(
            PMA_Directory,
            /*ReadWrite*/ 1,
            PAGE_PRIVILEGE_KERNEL,
            /*WriteThrough*/ 0,
            /*CacheDisabled*/ 0,
            /*Global*/ 0,
            /*Fixed*/ 1));

    // Fill identity-mapped low table (0..4MB) - manual setup like AllocPageDirectory
    LINEAR VMA_PT = MapTempPhysicalPage2(PMA_LowTable);
    if (VMA_PT == NULL) {
        ERROR(TEXT("[AllocUserPageDirectory] MapTempPhysicalPage2 failed on LowTable"));
        goto Out_Error;
    }
    LowTable = (LPPAGE_TABLE)VMA_PT;
    MemorySet(LowTable, 0, PAGE_SIZE);

    // Initialize identity mapping for 0..4MB (same as AllocPageDirectory)
    for (Index = 0; Index < PAGE_TABLE_NUM_ENTRIES; Index++) {
        PHYSICAL Physical = (PHYSICAL)Index << PAGE_SIZE_MUL;

        #ifdef PROTECT_BIOS
        BOOL Protected = Physical == 0 || (Physical > PROTECTED_ZONE_START && Physical <= PROTECTED_ZONE_END);
        #else
        BOOL Protected = FALSE;
        #endif

        if (Protected) {
            ClearPageTableEntry(LowTable, Index);
        } else {
            WritePageTableEntryValue(
                LowTable,
                Index,
                MakePageTableEntryValue(
                    Physical,
                    /*ReadWrite*/ 1,
                    PAGE_PRIVILEGE_KERNEL,
                    /*WriteThrough*/ 0,
                    /*CacheDisabled*/ 0,
                    /*Global*/ 0,
                    /*Fixed*/ 1));
        }
    }

    DEBUG(TEXT("[AllocUserPageDirectory] Low memory table copied from current"));

    // Fill kernel mapping table by copying the current kernel PT
    VMA_PT = MapTempPhysicalPage2(PMA_KernelTable);
    if (VMA_PT == NULL) {
        ERROR(TEXT("[AllocUserPageDirectory] MapTempPhysicalPage2 failed on KernelTable"));
        goto Out_Error;
    }
    KernelTable = (LPPAGE_TABLE)VMA_PT;

    // Create basic static kernel mapping instead of copying (for testing)
    MemorySet(KernelTable, 0, PAGE_SIZE);

    // Map full 4MB kernel space (1024 pages)
    for (Index = 0; Index < PAGE_TABLE_NUM_ENTRIES; Index++) {
        PHYSICAL Physical = PhysBaseKernel + ((PHYSICAL)Index << PAGE_SIZE_MUL);
        WritePageTableEntryValue(
            KernelTable,
            Index,
            MakePageTableEntryValue(
                Physical,
                /*ReadWrite*/ 1,
                PAGE_PRIVILEGE_KERNEL,
                /*WriteThrough*/ 0,
                /*CacheDisabled*/ 0,
                /*Global*/ 0,
                /*Fixed*/ 1));
    }

    DEBUG(TEXT("[AllocUserPageDirectory] Basic kernel mapping created"));

    // TLB sync before returning
    FlushTLB();

    DEBUG(TEXT("[AllocUserPageDirectory] PDE[0]=%x, PDE[768]=%x, PDE[1023]=%x"), ReadPageDirectoryEntryValue(Directory, 0),
        ReadPageDirectoryEntryValue(Directory, 768), ReadPageDirectoryEntryValue(Directory, 1023));
    DEBUG(TEXT("[AllocUserPageDirectory] LowTable[0]=%x, KernelTable[0]=%x"), ReadPageTableEntryValue(LowTable, 0),
        ReadPageTableEntryValue(KernelTable, 0));

    DEBUG(TEXT("[AllocUserPageDirectory] Exit"));
    return PMA_Directory;

Out_Error:

    if (PMA_Directory) FreePhysicalPage(PMA_Directory);
    if (PMA_LowTable) FreePhysicalPage(PMA_LowTable);
    if (PMA_KernelTable) FreePhysicalPage(PMA_KernelTable);

    return NULL;
}

/************************************************************************/

/**
 * @brief Allocate a page table for the given base address.
 * @param Base Base linear address.
 * @return Linear address of the new table or 0.
 */
LINEAR AllocPageTable(LINEAR Base) {
    PHYSICAL PMA_Table = AllocPhysicalPage();

    if (PMA_Table == NULL) {
        ERROR(TEXT("[AllocPageTable] Out of physical pages"));
        return NULL;
    }

    // Fill the directory entry that describes the new table
    UINT DirEntry = GetDirectoryEntry(Base);
    LPPAGE_DIRECTORY Directory = GetCurrentPageDirectoryVA();

    // Determine privilege: user space (< VMA_KERNEL) needs user privilege
    U32 Privilege = PAGE_PRIVILEGE(Base);

    WritePageDirectoryEntryValue(
        Directory,
        DirEntry,
        MakePageDirectoryEntryValue(
            PMA_Table,
            /*ReadWrite*/ 1,
            Privilege,
            /*WriteThrough*/ 0,
            /*CacheDisabled*/ 0,
            /*Global*/ 0,
            /*Fixed*/ 1));

    // Clear the new table by mapping its physical page temporarily.
    LINEAR VMA_PT = MapTempPhysicalPage2(PMA_Table);
    MemorySet((LPVOID)VMA_PT, 0, PAGE_SIZE);

    // Flush the Translation Look-up Buffer of the CPU
    FlushTLB();

    // Return the linear address of the table via the recursive window
    return (LINEAR)GetPageTableVAFor(Base);
}

/************************************************************************/

/**
 * @brief Check if a linear region is free of mappings.
 * @param Base Base linear address.
 * @param Size Size of region.
 * @return TRUE if region is free.
 */
BOOL IsRegionFree(LINEAR Base, UINT Size) {
    // DEBUG(TEXT("[IsRegionFree] Enter : %x; %x"), Base, Size);

    UINT NumPages = (Size + PAGE_SIZE - 1) >> PAGE_SIZE_MUL;
    LPPAGE_DIRECTORY Directory = GetCurrentPageDirectoryVA();
    ARCH_PAGE_ITERATOR Iterator = MemoryPageIteratorFromLinear(Base);

    // DEBUG(TEXT("[IsRegionFree] Traversing pages"));

    for (UINT i = 0; i < NumPages; i++) {
        UINT DirEntry = MemoryPageIteratorGetDirectoryIndex(&Iterator);
        UINT TabEntry = MemoryPageIteratorGetTableIndex(&Iterator);

        if (PageDirectoryEntryIsPresent(Directory, DirEntry)) {
            LPPAGE_TABLE Table = MemoryPageIteratorGetTable(&Iterator);
            if (PageTableEntryIsPresent(Table, TabEntry)) return FALSE;
        }

        MemoryPageIteratorStepPage(&Iterator);
    }

    // DEBUG(TEXT("[IsRegionFree] Exit"));

    return TRUE;
}

/************************************************************************/

/**
 * @brief Find a free linear region starting from a base address.
 * @param StartBase Starting linear address.
 * @param Size Desired region size.
 * @return Base of free region or 0.
 */
static LINEAR FindFreeRegion(LINEAR StartBase, UINT Size) {
    LINEAR Base = N_4MB;

    DEBUG(TEXT("[FindFreeRegion] Enter"));

    if (StartBase != 0) {
        LINEAR CanonStart = CanonicalizeLinearAddress(StartBase);
        if (CanonStart >= Base) {
            Base = CanonStart;
        }
        DEBUG(TEXT("[FindFreeRegion] Starting at %x"), Base);
    }

    while (TRUE) {
        if (IsRegionFree(Base, Size) == TRUE) return Base;

        LINEAR NextBase = CanonicalizeLinearAddress(Base + PAGE_SIZE);
        if (NextBase <= Base) {
            DEBUG(TEXT("[FindFreeRegion] Address space exhausted"));
            return NULL;
        }

        Base = NextBase;
    }
}

/************************************************************************/

/**
 * @brief Release page tables that no longer contain mappings.
 */
static void FreeEmptyPageTables(void) {
    LPPAGE_DIRECTORY Directory = GetCurrentPageDirectoryVA();
    ARCH_PAGE_ITERATOR Iterator = MemoryPageIteratorFromLinear(N_4MB);
    MemoryPageIteratorAlignToTableStart(&Iterator);

    while (MemoryPageIteratorGetLinear(&Iterator) < VMA_KERNEL) {
        UINT DirEntry = MemoryPageIteratorGetDirectoryIndex(&Iterator);
        PHYSICAL TablePhysical = PageDirectoryEntryGetPhysical(Directory, DirEntry);

        if (TablePhysical != 0) {
            LPPAGE_TABLE Table = MemoryPageIteratorGetTable(&Iterator);

            if (ArchPageTableIsEmpty(Table)) {
                SetPhysicalPageMark((UINT)(TablePhysical >> PAGE_SIZE_MUL), 0);
                ClearPageDirectoryEntry(Directory, DirEntry);
            }
        }

        MemoryPageIteratorNextTable(&Iterator);
    }
}

/************************************************************************/

/**
 * @brief Translate a linear address to its physical counterpart (page-level granularity).
 * @param Address Linear address.
 * @return Physical page number or MAX_U32 on failure.
 */
PHYSICAL MapLinearToPhysical(LINEAR Address) {
    LPPAGE_DIRECTORY Directory = GetCurrentPageDirectoryVA();
    ARCH_PAGE_ITERATOR Iterator = MemoryPageIteratorFromLinear(Address);
    UINT DirEntry = MemoryPageIteratorGetDirectoryIndex(&Iterator);
    UINT TabEntry = MemoryPageIteratorGetTableIndex(&Iterator);

    if (!PageDirectoryEntryIsPresent(Directory, DirEntry)) return 0;

    LPPAGE_TABLE Table = MemoryPageIteratorGetTable(&Iterator);
    if (!PageTableEntryIsPresent(Table, TabEntry)) return 0;

    PHYSICAL PagePhysical = PageTableEntryGetPhysical(Table, TabEntry);
    if (PagePhysical == 0) return 0;

    /* Compose physical: page frame | offset-in-page */
    return (PHYSICAL)(PagePhysical | (Address & (PAGE_SIZE - 1)));
}

/************************************************************************/

static BOOL PopulateRegionPages(LINEAR Base,
                                PHYSICAL Target,
                                UINT NumPages,
                                U32 Flags,
                                LINEAR RollbackBase,
                                LPCSTR FunctionName) {
    LPPAGE_DIRECTORY Directory = GetCurrentPageDirectoryVA();
    LPPAGE_TABLE Table = NULL;
    PHYSICAL Physical = NULL;
    U32 ReadWrite = (Flags & ALLOC_PAGES_READWRITE) ? 1 : 0;
    U32 PteCacheDisabled = (Flags & ALLOC_PAGES_UC) ? 1 : 0;
    U32 PteWriteThrough = (Flags & ALLOC_PAGES_WC) ? 1 : 0;

    if (PteCacheDisabled) PteWriteThrough = 0;

    ARCH_PAGE_ITERATOR Iterator = MemoryPageIteratorFromLinear(Base);

    for (UINT Index = 0; Index < NumPages; Index++) {
        UINT DirEntry = MemoryPageIteratorGetDirectoryIndex(&Iterator);
        UINT TabEntry = MemoryPageIteratorGetTableIndex(&Iterator);
        LINEAR CurrentLinear = MemoryPageIteratorGetLinear(&Iterator);

        if (!PageDirectoryEntryIsPresent(Directory, DirEntry)) {
            if (AllocPageTable(CurrentLinear) == NULL) {
                FreeRegion(RollbackBase, (UINT)(Index << PAGE_SIZE_MUL));
                DEBUG(TEXT("[%s] AllocPageTable failed"), FunctionName);
                return FALSE;
            }
        }

        Table = MemoryPageIteratorGetTable(&Iterator);
        U32 Privilege = PAGE_PRIVILEGE(CurrentLinear);
        U32 FixedFlag = (Flags & ALLOC_PAGES_IO) ? 1u : 0u;
        U32 BaseFlags = BuildPageFlags(ReadWrite, Privilege, PteWriteThrough, PteCacheDisabled, 0, FixedFlag);
        U32 ReservedFlags = BaseFlags & ~PAGE_FLAG_PRESENT;
        PHYSICAL ReservedPhysical = (PHYSICAL)(MAX_U32 & ~(PAGE_SIZE - 1));

        WritePageTableEntryValue(Table, TabEntry, MakePageEntryRaw(ReservedPhysical, ReservedFlags));

        if (Flags & ALLOC_PAGES_COMMIT) {
            if (Target != 0) {
                Physical = Target + (PHYSICAL)(Index << PAGE_SIZE_MUL);

                if (Flags & ALLOC_PAGES_IO) {
                    WritePageTableEntryValue(
                        Table,
                        TabEntry,
                        MakePageTableEntryValue(
                            Physical,
                            ReadWrite,
                            Privilege,
                            PteWriteThrough,
                            PteCacheDisabled,
                            /*Global*/ 0,
                            /*Fixed*/ 1));
                } else {
                    SetPhysicalPageMark((UINT)(Physical >> PAGE_SIZE_MUL), 1);
                    WritePageTableEntryValue(
                        Table,
                        TabEntry,
                        MakePageTableEntryValue(
                            Physical,
                            ReadWrite,
                            Privilege,
                            PteWriteThrough,
                            PteCacheDisabled,
                            /*Global*/ 0,
                            /*Fixed*/ 0));
                }
            } else {
                Physical = AllocPhysicalPage();

                if (Physical == NULL) {
                    ERROR(TEXT("[%s] AllocPhysicalPage failed"), FunctionName);
                    FreeRegion(RollbackBase, (UINT)(Index << PAGE_SIZE_MUL));
                    return FALSE;
                }

                WritePageTableEntryValue(
                    Table,
                    TabEntry,
                    MakePageTableEntryValue(
                        Physical,
                        ReadWrite,
                        Privilege,
                        PteWriteThrough,
                        PteCacheDisabled,
                        /*Global*/ 0,
                        /*Fixed*/ 0));
            }
        }

        MemoryPageIteratorStepPage(&Iterator);
        Base += PAGE_SIZE;
    }

    return TRUE;
}

/************************************************************************/

/**
 * @brief Allocate and map a physical region into the linear address space.
 * @param Base Desired base address or 0. When zero and ALLOC_PAGES_AT_OR_OVER
 *             is not set, the allocator picks any free region.
 * @param Target Desired physical base address or 0. Requires
 *               ALLOC_PAGES_COMMIT when specified. Use with ALLOC_PAGES_IO to
 *               map device memory without touching the physical bitmap.
 * @param Size Size in bytes, rounded up to page granularity. Limited to 25% of
 *             the available physical memory.
 * @param Flags Mapping flags:
 *              - ALLOC_PAGES_COMMIT: allocate and map backing pages.
 *              - ALLOC_PAGES_READWRITE: request writable pages (read-only
 *                otherwise).
 *              - ALLOC_PAGES_AT_OR_OVER: accept any region starting at or
 *                above Base.
 *              - ALLOC_PAGES_UC / ALLOC_PAGES_WC: control cache attributes
 *                (UC has priority over WC).
 *              - ALLOC_PAGES_IO: keep physical pages marked fixed for MMIO.
 * @return Allocated linear base address or 0 on failure.
 */
LINEAR AllocRegion(LINEAR Base, PHYSICAL Target, UINT Size, U32 Flags) {
    LINEAR Pointer = NULL;
    UINT NumPages = 0;
    DEBUG(TEXT("[AllocRegion] Enter: Base=%x Target=%x Size=%x Flags=%x"), Base, Target, Size, Flags);

    // Can't allocate more than 25% of total memory at once
    if (Size > KernelStartup.MemorySize / 4) {
        ERROR(TEXT("[AllocRegion] Size %x exceeds 25%% of memory (%x)"), Size, KernelStartup.MemorySize / 4);
        return NULL;
    }

    // Rounding behavior for page count
    NumPages = (Size + (PAGE_SIZE - 1)) >> PAGE_SIZE_MUL;  // ceil(Size / 4096)
    if (NumPages == 0) NumPages = 1;

    Base = CanonicalizeLinearAddress(Base);

    // If an exact physical mapping is requested, validate inputs
    if (Target != 0) {
        if ((Target & (PAGE_SIZE - 1)) != 0) {
            ERROR(TEXT("[AllocRegion] Target not page-aligned (%x)"), Target);
            return NULL;
        }

        if ((Flags & ALLOC_PAGES_IO) == 0 && (Flags & ALLOC_PAGES_COMMIT) == 0) {
            ERROR(TEXT("[AllocRegion] Exact PMA mapping requires COMMIT"));
            return NULL;
        }

        if (ValidatePhysicalTargetRange(Target, NumPages) == FALSE) {
            ERROR(TEXT("[AllocRegion] Target range cannot be addressed"));
            return NULL;
        }
        /* NOTE: Do not reject pages already marked used here.
           Target may come from AllocPhysicalPage(), which marks the page in the bitmap.
           We will just map it and keep the mark consistent. */
    }

    /* If the calling process requests that a linear address be mapped,
       see if the region is not already allocated. */
    if (Base != 0 && (Flags & ALLOC_PAGES_AT_OR_OVER) == 0) {
        if (IsRegionFree(Base, Size) == FALSE) {
            DEBUG(TEXT("[AllocRegion] No free region found with specified base : %x"), Base);
            return NULL;
        }
    }

    /* If the calling process does not care about the base address of
       the region, try to find a region which is at least as large as
       the "Size" parameter. */
    if (Base == 0 || (Flags & ALLOC_PAGES_AT_OR_OVER)) {
        DEBUG(TEXT("[AllocRegion] Calling FindFreeRegion with base = %x and size = %x"), Base, Size);

        LINEAR NewBase = FindFreeRegion(Base, Size);

        if (NewBase == NULL) {
            DEBUG(TEXT("[AllocRegion] No free region found with unspecified base from %x"), Base);
            return NULL;
        }

        Base = NewBase;

        DEBUG(TEXT("[AllocRegion] FindFreeRegion found with base = %x and size = %x"), Base, Size);
    }

    // Set the return value to "Base".
    Pointer = Base;

    DEBUG(TEXT("[AllocRegion] Allocating pages"));

    if (PopulateRegionPages(Base, Target, NumPages, Flags, Pointer, TEXT("AllocRegion")) == FALSE) {
        return NULL;
    }

    // Flush the Translation Look-up Buffer of the CPU
    FlushTLB();

    DEBUG(TEXT("[AllocRegion] Exit"));

    return Pointer;
}

/************************************************************************/

/**
 * @brief Resize an existing linear region.
 * @param Base Base linear address of the region.
 * @param Target Physical base address or 0. Must match the existing mapping
 *               when resizing committed regions.
 * @param Size Current size in bytes.
 * @param NewSize Desired size in bytes.
 * @param Flags Mapping flags used for the region (see AllocRegion).
 * @return TRUE on success, FALSE otherwise.
 */
BOOL ResizeRegion(LINEAR Base, PHYSICAL Target, UINT Size, UINT NewSize, U32 Flags) {
    DEBUG(TEXT("[ResizeRegion] Enter: Base=%x Target=%x Size=%x NewSize=%x Flags=%x"),
          Base,
          Target,
          Size,
          NewSize,
          Flags);

    if (Base == 0) {
        ERROR(TEXT("[ResizeRegion] Base cannot be null"));
        return FALSE;
    }

    if (NewSize > KernelStartup.MemorySize / 4) {
        ERROR(TEXT("[ResizeRegion] New size %x exceeds 25%% of memory (%x)"),
              NewSize,
              KernelStartup.MemorySize / 4);
        return FALSE;
    }

    UINT CurrentPages = (Size + (PAGE_SIZE - 1)) >> PAGE_SIZE_MUL;
    UINT RequestedPages = (NewSize + (PAGE_SIZE - 1)) >> PAGE_SIZE_MUL;
    if (CurrentPages == 0) CurrentPages = 1;
    if (RequestedPages == 0) RequestedPages = 1;

    if (RequestedPages == CurrentPages) {
        DEBUG(TEXT("[ResizeRegion] No page count change"));
        return TRUE;
    }

    if (RequestedPages > CurrentPages) {
        UINT AdditionalPages = RequestedPages - CurrentPages;
        LINEAR NewBase = Base + ((LINEAR)CurrentPages << PAGE_SIZE_MUL);
        UINT AdditionalSize = AdditionalPages << PAGE_SIZE_MUL;

        if (IsRegionFree(NewBase, AdditionalSize) == FALSE) {
            DEBUG(TEXT("[ResizeRegion] Additional region not free at %x"), NewBase);
            return FALSE;
        }

        PHYSICAL AdditionalTarget = 0;
        if (Target != 0) {
            AdditionalTarget = Target + (PHYSICAL)(CurrentPages << PAGE_SIZE_MUL);
        }

        DEBUG(TEXT("[ResizeRegion] Expanding region by %x bytes"), AdditionalSize);

        if (PopulateRegionPages(NewBase,
                                AdditionalTarget,
                                AdditionalPages,
                                Flags,
                                NewBase,
                                TEXT("ResizeRegion")) == FALSE) {
            return FALSE;
        }

        FlushTLB();
    } else {
        UINT PagesToRelease = CurrentPages - RequestedPages;
        if (PagesToRelease != 0) {
            LINEAR ReleaseBase = Base + ((LINEAR)RequestedPages << PAGE_SIZE_MUL);
            UINT ReleaseSize = PagesToRelease << PAGE_SIZE_MUL;

            DEBUG(TEXT("[ResizeRegion] Shrinking region by %x bytes"), ReleaseSize);
            FreeRegion(ReleaseBase, ReleaseSize);
        }
    }

    DEBUG(TEXT("[ResizeRegion] Exit"));
    return TRUE;
}

/************************************************************************/

/**
 * @brief Resolve the page table targeted by an iterator when the hierarchy is present.
 * @param Iterator Page iterator referencing the page to access.
 * @param OutTable Receives the page table pointer when available.
 * @return TRUE when the table exists and is returned.
 */
static BOOL TryGetPageTableForIterator(const ARCH_PAGE_ITERATOR* Iterator, LPPAGE_TABLE* OutTable) {
    if (Iterator == NULL || OutTable == NULL) return FALSE;

#if defined(__EXOS_ARCH_X86_64__)
    LINEAR Linear = CanonicalizeLinearAddress((LINEAR)MemoryPageIteratorGetLinear(Iterator));
    LPPML4 Pml4 = GetCurrentPml4VA();
    UINT Pml4Index = MemoryPageIteratorGetPml4Index(Iterator);

    if ((ReadPageDirectoryEntryValue((LPPAGE_DIRECTORY)Pml4, Pml4Index) & PAGE_FLAG_PRESENT) == 0) {
        return FALSE;
    }

    LPPDPT Pdpt = GetPageDirectoryPointerTableVAFor(Linear);
    UINT PdptIndex = MemoryPageIteratorGetPdptIndex(Iterator);

    if ((ReadPageDirectoryEntryValue((LPPAGE_DIRECTORY)Pdpt, PdptIndex) & PAGE_FLAG_PRESENT) == 0) {
        return FALSE;
    }

    LPPAGE_DIRECTORY Directory = GetPageDirectoryVAFor(Linear);
#else
    LPPAGE_DIRECTORY Directory = GetCurrentPageDirectoryVA();
#endif

    UINT DirEntry = MemoryPageIteratorGetDirectoryIndex(Iterator);

    if (!PageDirectoryEntryIsPresent(Directory, DirEntry)) {
        return FALSE;
    }

    *OutTable = MemoryPageIteratorGetTable(Iterator);
    return TRUE;
}

/************************************************************************/

/**
 * @brief Unmap and free a linear region.
 * @param Base Base linear address.
 * @param Size Size of region.
 * @return TRUE on success.
 */
BOOL FreeRegion(LINEAR Base, UINT Size) {
    LPPAGE_TABLE Table = NULL;
    UINT NumPages = 0;

    NumPages = (Size + (PAGE_SIZE - 1)) >> PAGE_SIZE_MUL; /* ceil(Size / 4096) */
    if (NumPages == 0) NumPages = 1;

    // Free each page in turn.
    Base = CanonicalizeLinearAddress(Base);
    ARCH_PAGE_ITERATOR Iterator = MemoryPageIteratorFromLinear(Base);

    for (UINT Index = 0; Index < NumPages; Index++) {
        UINT TabEntry = MemoryPageIteratorGetTableIndex(&Iterator);

        if (TryGetPageTableForIterator(&Iterator, &Table) && PageTableEntryIsPresent(Table, TabEntry)) {
            PHYSICAL EntryPhysical = PageTableEntryGetPhysical(Table, TabEntry);

            /* Skip bitmap mark if it was an IO mapping (BAR) */
            if (!PageTableEntryIsFixed(Table, TabEntry)) {
                SetPhysicalPageMark((UINT)(EntryPhysical >> PAGE_SIZE_MUL), 0);
            }

            ClearPageTableEntry(Table, TabEntry);
        }

        MemoryPageIteratorStepPage(&Iterator);
        Base += PAGE_SIZE;
    }

    FreeEmptyPageTables();

    // Flush the Translation Look-up Buffer of the CPU
    FlushTLB();

    return TRUE;
}

/************************************************************************/

/**
 * @brief Map an I/O physical range into virtual memory.
 * @param PhysicalBase Physical base address.
 * @param Size Size in bytes.
 * @return Linear address or 0 on failure.
 */
LINEAR MapIOMemory(PHYSICAL PhysicalBase, UINT Size) {
    // Basic parameter checks
    if (PhysicalBase == 0 || Size == 0) {
        ERROR(TEXT("[MapIOMemory] Invalid parameters (PA=%x Size=%x)"), PhysicalBase, Size);
        return NULL;
    }

    // Calculate page-aligned base and adjusted size for non-aligned addresses
    UINT PageOffset = (UINT)(PhysicalBase & (PAGE_SIZE - 1));
    PHYSICAL AlignedPhysicalBase = PhysicalBase & ~(PAGE_SIZE - 1);
    UINT AdjustedSize = ((Size + PageOffset + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1));

    DEBUG(TEXT("[MapIOMemory] Original: PA=%x Size=%x"), PhysicalBase, Size);
    DEBUG(TEXT("[MapIOMemory] Aligned: PA=%x Size=%x Offset=%x"), AlignedPhysicalBase, AdjustedSize, PageOffset);

    // Map as Uncached, Read/Write, exact PMA mapping, IO semantics
    LINEAR AlignedResult = AllocRegion(
        VMA_KERNEL,          // Start search in kernel space to avoid user space
        AlignedPhysicalBase, // Page-aligned PMA
        AdjustedSize,        // Page-aligned size
        ALLOC_PAGES_COMMIT | ALLOC_PAGES_READWRITE | ALLOC_PAGES_UC |  // MMIO must be UC
            ALLOC_PAGES_IO |
            ALLOC_PAGES_AT_OR_OVER  // Do not touch RAM bitmap; mark PTE.Fixed; search at or over VMA_KERNEL
    );

    if (AlignedResult == NULL) {
        DEBUG(TEXT("[MapIOMemory] AllocRegion failed"));
        return NULL;
    }

    // Return the address adjusted for the original offset
    LINEAR CanonicalAligned = CanonicalizeLinearAddress(AlignedResult);
    LINEAR result = CanonicalizeLinearAddress(CanonicalAligned + (LINEAR)PageOffset);
    DEBUG(TEXT("[MapIOMemory] Mapped at aligned=%x, returning=%x"), AlignedResult, result);
    return result;
}

/************************************************************************/

/**
 * @brief Unmap a previously mapped I/O range.
 * @param LinearBase Linear base address.
 * @param Size Size in bytes.
 * @return TRUE on success.
 */
BOOL UnMapIOMemory(LINEAR LinearBase, UINT Size) {
    // Basic parameter checks
    if (LinearBase == 0 || Size == 0) {
        ERROR(TEXT("[UnMapIOMemory] Invalid parameters (LA=%x Size=%x)"), LinearBase, Size);
        return FALSE;
    }

    // Just unmap; FreeRegion will skip RAM bitmap if PTE.Fixed was set
    return FreeRegion(CanonicalizeLinearAddress(LinearBase), Size);
}

/************************************************************************/

/**
 * @brief Allocate a kernel region - wrapper around AllocRegion with VMA_KERNEL and AT_OR_OVER.
 * @param Target Physical base address (0 for any).
 * @param Size Size in bytes.
 * @param Flags Additional allocation flags.
 * @return Linear address or 0 on failure.
 */
LINEAR AllocKernelRegion(PHYSICAL Target, UINT Size, U32 Flags) {
    // Always use VMA_KERNEL base and add AT_OR_OVER flag
    return AllocRegion(VMA_KERNEL, Target, Size, Flags | ALLOC_PAGES_AT_OR_OVER);
}

/************************************************************************/

/**
 * @brief Initialize the kernel memory manager.
 */
void InitializeMemoryManager(void) {
    MemoryArchInitializeManager();
}

