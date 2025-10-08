
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
#include "arch/i386/I386.h"
#include "Log.h"
#include "arch/i386/LogI386Struct.h"
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
/// Architecture-specific constants are defined in arch/i386/Memory-i386.h.

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
 * @brief Clip a 64-bit range to 32 bits.
 * @param base Input base address.
 * @param len Length of the range.
 * @param outBase Resulting 32-bit base.
 * @param outLen Resulting 32-bit length.
 * @return Non-zero if clipping succeeded.
 */
static inline int ClipTo32Bit(U64 base, U64 len, PHYSICAL* outBase, UINT* outLen) {
    U64 limit = U64_Make(1, 0x00000000u);
    if (len.HI == 0 && len.LO == 0) return 0;
    if (U64_Cmp(base, limit) >= 0) return 0;
    U64 end = U64_Add(base, len);
    if (U64_Cmp(end, limit) > 0) end = limit;
    U64 newLen = U64_Sub(end, base);

    if (newLen.HI != 0) {
        *outBase = base.LO;
        *outLen = 0xFFFFFFFFu - base.LO;
    } else {
        *outBase = base.LO;
        *outLen = newLen.LO;
    }
    return (*outLen != 0);
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
static void MarkUsedPhysicalMemory(void) {
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

            ClipTo32Bit(Entry->Base, Entry->Size, &Base, &Size);

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
// Paging helpers are provided by arch/i386/Memory-i386.h.
/************************************************************************/

/************************************************************************/
// Map or remap a single virtual page by directly editing its PTE via the self-map.

static inline void MapOnePage(
    LINEAR Linear, PHYSICAL Physical, U32 ReadWrite, U32 Privilege, U32 WriteThrough, U32 CacheDisabled, U32 Global,
    U32 Fixed) {
    volatile U32* Pte = GetPageTableEntryRawPointer(Linear);
    LPPAGE_DIRECTORY Directory = GetCurrentPageDirectoryVA();
    UINT dir = GetDirectoryEntry(Linear);

    if (!Directory[dir].Present) {
        ERROR(TEXT("[MapOnePage] PDE not present for VA %x (dir=%d)"), Linear, dir);
        return;  // Or panic
    }

    *Pte = MakePageTableEntryValue(Physical, ReadWrite, Privilege, WriteThrough, CacheDisabled, Global, Fixed);
    InvalidatePage(Linear);
}

/************************************************************************/

/**
 * @brief Unmap a single page from the current address space.
 * @param Linear Linear address to unmap.
 */
static inline void UnmapOnePage(LINEAR Linear) {
    volatile U32* Pte = GetPageTableEntryRawPointer(Linear);
    *Pte = 0;
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
    if (Directory[dir].Present == 0) return FALSE;

    // Page table present?
    LPPAGE_TABLE Table = GetPageTableVAFor(Pointer);
    if (Table[tab].Present == 0) return FALSE;

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
    Directory[0].Present = 1;
    Directory[0].ReadWrite = 1;
    Directory[0].Privilege = PAGE_PRIVILEGE_KERNEL;
    Directory[0].WriteThrough = 0;
    Directory[0].CacheDisabled = 0;
    Directory[0].Accessed = 0;
    Directory[0].Reserved = 0;
    Directory[0].PageSize = 0;  // 4KB pages
    Directory[0].Global = 0;
    Directory[0].User = 0;
    Directory[0].Fixed = 1;
    Directory[0].Address = (PMA_LowTable >> PAGE_SIZE_MUL);

    // Directory[DirKernel] -> map VMA_KERNEL..VMA_KERNEL+4MB-1 to KERNEL_PHYSICAL_ORIGIN..+4MB-1
    Directory[DirKernel].Present = 1;
    Directory[DirKernel].ReadWrite = 1;
    Directory[DirKernel].Privilege = PAGE_PRIVILEGE_KERNEL;
    Directory[DirKernel].WriteThrough = 0;
    Directory[DirKernel].CacheDisabled = 0;
    Directory[DirKernel].Accessed = 0;
    Directory[DirKernel].Reserved = 0;
    Directory[DirKernel].PageSize = 0;  // 4KB pages
    Directory[DirKernel].Global = 0;
    Directory[DirKernel].User = 0;
    Directory[DirKernel].Fixed = 1;
    Directory[DirKernel].Address = (PMA_KernelTable >> PAGE_SIZE_MUL);

    // Directory[DirTaskRunner] -> map VMA_TASK_RUNNER (one page) to TaskRunner physical location
    Directory[DirTaskRunner].Present = 1;
    Directory[DirTaskRunner].ReadWrite = 1;
    Directory[DirTaskRunner].Privilege = PAGE_PRIVILEGE_USER;
    Directory[DirTaskRunner].WriteThrough = 0;
    Directory[DirTaskRunner].CacheDisabled = 0;
    Directory[DirTaskRunner].Accessed = 0;
    Directory[DirTaskRunner].Reserved = 0;
    Directory[DirTaskRunner].PageSize = 0;  // 4KB pages
    Directory[DirTaskRunner].Global = 0;
    Directory[DirTaskRunner].User = 0;
    Directory[DirTaskRunner].Fixed = 1;
    Directory[DirTaskRunner].Address = (PMA_TaskRunnerTable >> PAGE_SIZE_MUL);

    // Install recursive mapping: PDE[1023] = PD
    Directory[PD_RECURSIVE_SLOT].Present = 1;
    Directory[PD_RECURSIVE_SLOT].ReadWrite = 1;
    Directory[PD_RECURSIVE_SLOT].Privilege = PAGE_PRIVILEGE_KERNEL;
    Directory[PD_RECURSIVE_SLOT].WriteThrough = 0;
    Directory[PD_RECURSIVE_SLOT].CacheDisabled = 0;
    Directory[PD_RECURSIVE_SLOT].Accessed = 0;
    Directory[PD_RECURSIVE_SLOT].Reserved = 0;
    Directory[PD_RECURSIVE_SLOT].PageSize = 0;
    Directory[PD_RECURSIVE_SLOT].Global = 0;
    Directory[PD_RECURSIVE_SLOT].User = 0;
    Directory[PD_RECURSIVE_SLOT].Fixed = 1;
    Directory[PD_RECURSIVE_SLOT].Address = (PMA_Directory >> PAGE_SIZE_MUL);

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
        #ifdef PROTECT_BIOS
        PHYSICAL Physical = (PHYSICAL)Index << PAGE_SIZE_MUL;
        BOOL Protected = Physical == 0 || (Physical > PROTECTED_ZONE_START && Physical <= PROTECTED_ZONE_END);
        #else
        BOOL Protected = FALSE;
        #endif

        LowTable[Index].Present = !Protected;
        LowTable[Index].ReadWrite = 1;
        LowTable[Index].Privilege = PAGE_PRIVILEGE_KERNEL;
        LowTable[Index].WriteThrough = 0;
        LowTable[Index].CacheDisabled = 0;
        LowTable[Index].Accessed = 0;
        LowTable[Index].Dirty = 0;
        LowTable[Index].Reserved = 0;
        LowTable[Index].Global = 0;
        LowTable[Index].User = 0;
        LowTable[Index].Fixed = 1;
        LowTable[Index].Address = Index;  // frame N -> 4KB*N
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

    UINT KernelFirstFrame = (UINT)(PhysBaseKernel >> PAGE_SIZE_MUL);
    for (Index = 0; Index < PAGE_TABLE_NUM_ENTRIES; Index++) {
        KernelTable[Index].Present = 1;
        KernelTable[Index].ReadWrite = 1;
        KernelTable[Index].Privilege = PAGE_PRIVILEGE_KERNEL;
        KernelTable[Index].WriteThrough = 0;
        KernelTable[Index].CacheDisabled = 0;
        KernelTable[Index].Accessed = 0;
        KernelTable[Index].Dirty = 0;
        KernelTable[Index].Reserved = 0;
        KernelTable[Index].Global = 0;
        KernelTable[Index].User = 0;
        KernelTable[Index].Fixed = 1;
        KernelTable[Index].Address = KernelFirstFrame + Index;
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

    TaskRunnerTable[TaskRunnerTableIndex].Present = 1;
    TaskRunnerTable[TaskRunnerTableIndex].ReadWrite = 0;  // Read-only for user
    TaskRunnerTable[TaskRunnerTableIndex].Privilege = PAGE_PRIVILEGE_USER;
    TaskRunnerTable[TaskRunnerTableIndex].WriteThrough = 0;
    TaskRunnerTable[TaskRunnerTableIndex].CacheDisabled = 0;
    TaskRunnerTable[TaskRunnerTableIndex].Accessed = 0;
    TaskRunnerTable[TaskRunnerTableIndex].Dirty = 0;
    TaskRunnerTable[TaskRunnerTableIndex].Reserved = 0;
    TaskRunnerTable[TaskRunnerTableIndex].Global = 0;
    TaskRunnerTable[TaskRunnerTableIndex].User = 0;
    TaskRunnerTable[TaskRunnerTableIndex].Fixed = 1;
    TaskRunnerTable[TaskRunnerTableIndex].Address = TaskRunnerPhysical >> PAGE_SIZE_MUL;

    // TLB sync before returning
    FlushTLB();

    DEBUG(TEXT("[AllocPageDirectory] PDE[0]=%x, PDE[768]=%x, PDE[%u]=%x, PDE[1023]=%x"), *(U32*)&Directory[0],
        *(U32*)&Directory[768], DirTaskRunner, *(U32*)&Directory[DirTaskRunner], *(U32*)&Directory[1023]);
    DEBUG(TEXT("[AllocPageDirectory] LowTable[0]=%x, KernelTable[0]=%x, TaskRunnerTable[%u]=%x"),
        *(U32*)&LowTable[0], *(U32*)&KernelTable[0], TaskRunnerTableIndex,
        *(U32*)&TaskRunnerTable[TaskRunnerTableIndex]);
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
    Directory[0].Present = 1;
    Directory[0].ReadWrite = 1;
    Directory[0].Privilege = PAGE_PRIVILEGE_KERNEL;
    Directory[0].WriteThrough = 0;
    Directory[0].CacheDisabled = 0;
    Directory[0].Accessed = 0;
    Directory[0].Reserved = 0;
    Directory[0].PageSize = 0;  // 4KB pages
    Directory[0].Global = 0;
    Directory[0].User = 0;
    Directory[0].Fixed = 1;
    Directory[0].Address = (PMA_LowTable >> PAGE_SIZE_MUL);

    // Directory[DirKernel] -> map VMA_KERNEL..VMA_KERNEL+4MB-1 to current kernel state
    Directory[DirKernel].Present = 1;
    Directory[DirKernel].ReadWrite = 1;
    Directory[DirKernel].Privilege = PAGE_PRIVILEGE_KERNEL;
    Directory[DirKernel].WriteThrough = 0;
    Directory[DirKernel].CacheDisabled = 0;
    Directory[DirKernel].Accessed = 0;
    Directory[DirKernel].Reserved = 0;
    Directory[DirKernel].PageSize = 0;  // 4KB pages
    Directory[DirKernel].Global = 0;
    Directory[DirKernel].User = 0;
    Directory[DirKernel].Fixed = 1;
    Directory[DirKernel].Address = (PMA_KernelTable >> PAGE_SIZE_MUL);

    // Copy present PDEs from current directory, but skip user space (VMA_USER to VMA_LIBRARY-1)
    // to allow new process to allocate its own region at VMA_USER
    UNUSED(VMA_TASK_RUNNER);
    UINT UserStartPDE = GetDirectoryEntry(VMA_USER);             // PDE index for VMA_USER
    UINT UserEndPDE = GetDirectoryEntry(VMA_LIBRARY - 1) - 1;    // PDE index for VMA_LIBRARY-1, excluding TaskRunner space
    for (Index = 1; Index < 1023; Index++) {                            // Skip 0 (already done) and 1023 (self-map)
        if (CurrentPD[Index].Present && Index != DirKernel) {
            // Skip user space PDEs to avoid copying current process's user space
            if (Index >= UserStartPDE && Index <= UserEndPDE) {
                DEBUG(TEXT("[AllocUserPageDirectory] Skipped user space PDE[%u]"), Index);
                continue;
            }
            Directory[Index] = CurrentPD[Index];
            DEBUG(TEXT("[AllocUserPageDirectory] Copied PDE[%u]"), Index);
        }
    }

    // Install recursive mapping: PDE[1023] = PD
    Directory[PD_RECURSIVE_SLOT].Present = 1;
    Directory[PD_RECURSIVE_SLOT].ReadWrite = 1;
    Directory[PD_RECURSIVE_SLOT].Privilege = PAGE_PRIVILEGE_KERNEL;
    Directory[PD_RECURSIVE_SLOT].WriteThrough = 0;
    Directory[PD_RECURSIVE_SLOT].CacheDisabled = 0;
    Directory[PD_RECURSIVE_SLOT].Accessed = 0;
    Directory[PD_RECURSIVE_SLOT].Reserved = 0;
    Directory[PD_RECURSIVE_SLOT].PageSize = 0;
    Directory[PD_RECURSIVE_SLOT].Global = 0;
    Directory[PD_RECURSIVE_SLOT].User = 0;
    Directory[PD_RECURSIVE_SLOT].Fixed = 1;
    Directory[PD_RECURSIVE_SLOT].Address = (PMA_Directory >> PAGE_SIZE_MUL);

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
        #ifdef PROTECT_BIOS
        PHYSICAL Physical = (PHYSICAL)Index << PAGE_SIZE_MUL;
        BOOL Protected = Physical == 0 || (Physical > PROTECTED_ZONE_START && Physical <= PROTECTED_ZONE_END);
        #else
        BOOL Protected = FALSE;
        #endif

        LowTable[Index].Present = !Protected;
        LowTable[Index].ReadWrite = 1;
        LowTable[Index].Privilege = PAGE_PRIVILEGE_KERNEL;
        LowTable[Index].WriteThrough = 0;
        LowTable[Index].CacheDisabled = 0;
        LowTable[Index].Accessed = 0;
        LowTable[Index].Dirty = 0;
        LowTable[Index].Reserved = 0;
        LowTable[Index].Global = 0;
        LowTable[Index].User = 0;
        LowTable[Index].Fixed = 1;
        LowTable[Index].Address = Index;  // Identity mapping: page Index -> physical page Index
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

    UINT KernelFirstFrame = (UINT)(PhysBaseKernel >> PAGE_SIZE_MUL);

    // Map full 4MB kernel space (1024 pages)
    for (Index = 0; Index < PAGE_TABLE_NUM_ENTRIES; Index++) {
        KernelTable[Index].Present = 1;
        KernelTable[Index].ReadWrite = 1;
        KernelTable[Index].Privilege = PAGE_PRIVILEGE_KERNEL;
        KernelTable[Index].WriteThrough = 0;
        KernelTable[Index].CacheDisabled = 0;
        KernelTable[Index].Accessed = 0;
        KernelTable[Index].Dirty = 0;
        KernelTable[Index].Reserved = 0;
        KernelTable[Index].Global = 0;
        KernelTable[Index].User = 0;
        KernelTable[Index].Fixed = 1;
        KernelTable[Index].Address = KernelFirstFrame + Index;
    }

    DEBUG(TEXT("[AllocUserPageDirectory] Basic kernel mapping created"));

    // TLB sync before returning
    FlushTLB();

    DEBUG(TEXT("[AllocUserPageDirectory] PDE[0]=%x, PDE[768]=%x, PDE[1023]=%x"), *(U32*)&Directory[0],
        *(U32*)&Directory[768], *(U32*)&Directory[1023]);
    DEBUG(TEXT("[AllocUserPageDirectory] LowTable[0]=%x, KernelTable[0]=%x"), *(U32*)&LowTable[0],
        *(U32*)&KernelTable[0]);

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

    Directory[DirEntry].Present = 1;
    Directory[DirEntry].ReadWrite = 1;
    Directory[DirEntry].Privilege = Privilege;
    Directory[DirEntry].WriteThrough = 0;
    Directory[DirEntry].CacheDisabled = 0;
    Directory[DirEntry].Accessed = 0;
    Directory[DirEntry].Reserved = 0;
    Directory[DirEntry].PageSize = 0;
    Directory[DirEntry].Global = 0;
    Directory[DirEntry].User = 0;
    Directory[DirEntry].Fixed = 1;
    Directory[DirEntry].Address = PMA_Table >> PAGE_SIZE_MUL;

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
    LINEAR Current = Base;

    // DEBUG(TEXT("[IsRegionFree] Traversing pages"));

    for (UINT i = 0; i < NumPages; i++) {
        UINT dir = GetDirectoryEntry(Current);
        UINT tab = GetTableEntry(Current);

        if (Directory[dir].Present) {
            LPPAGE_TABLE Table = GetPageTableVAFor(Current);
            if (Table[tab].Present) return FALSE;
        }

        Current += PAGE_SIZE;
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

    if (StartBase >= Base) {
        DEBUG(TEXT("[FindFreeRegion] Starting at %x"), StartBase);
        Base = StartBase;
    }

    FOREVER {
        if (IsRegionFree(Base, Size) == TRUE) return Base;
        Base += PAGE_SIZE;
    }

    DEBUG(TEXT("[FindFreeRegion] Exit"));

    return NULL;
}

/************************************************************************/

/**
 * @brief Release page tables that no longer contain mappings.
 */
static void FreeEmptyPageTables(void) {
    LPPAGE_DIRECTORY Directory = GetCurrentPageDirectoryVA();
    LPPAGE_TABLE Table = NULL;
    LINEAR Base = N_4MB;
    UINT DirEntry = 0;
    UINT Index = 0;
    BOOL DestroyIt = TRUE;

    while (Base < VMA_KERNEL) {
        DestroyIt = TRUE;
        DirEntry = GetDirectoryEntry(Base);

        if (Directory[DirEntry].Address != NULL) {
            Table = GetPageTableVAFor(Base);

            for (Index = 0; Index < PAGE_TABLE_NUM_ENTRIES; Index++) {
                if (Table[Index].Address != NULL) DestroyIt = FALSE;
            }

            if (DestroyIt) {
                SetPhysicalPageMark((UINT)Directory[DirEntry].Address, 0);
                Directory[DirEntry].Present = 0;
                Directory[DirEntry].Address = NULL;
            }
        }

        Base += PAGE_TABLE_CAPACITY;
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
    UINT DirEntry = GetDirectoryEntry(Address);
    UINT TabEntry = GetTableEntry(Address);

    if (Directory[DirEntry].Address == 0) return 0;

    LPPAGE_TABLE Table = GetPageTableVAFor(Address);
    if (Table[TabEntry].Address == 0) return 0;

    /* Compose physical: page frame << 12 | offset-in-page */
    return (PHYSICAL)((Table[TabEntry].Address << PAGE_SIZE_MUL) | (Address & (PAGE_SIZE - 1)));
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

    for (UINT Index = 0; Index < NumPages; Index++) {
        UINT DirEntry = GetDirectoryEntry(Base);
        UINT TabEntry = GetTableEntry(Base);

        if (Directory[DirEntry].Address == NULL) {
            if (AllocPageTable(Base) == NULL) {
                FreeRegion(RollbackBase, (UINT)(Index << PAGE_SIZE_MUL));
                DEBUG(TEXT("[%s] AllocPageTable failed"), FunctionName);
                return FALSE;
            }
        }

        Table = GetPageTableVAFor(Base);

        Table[TabEntry].Present = 0;
        Table[TabEntry].ReadWrite = ReadWrite;
        Table[TabEntry].Privilege = PAGE_PRIVILEGE(Base);
        Table[TabEntry].WriteThrough = PteWriteThrough;
        Table[TabEntry].CacheDisabled = PteCacheDisabled;
        Table[TabEntry].Accessed = 0;
        Table[TabEntry].Dirty = 0;
        Table[TabEntry].Reserved = 0;
        Table[TabEntry].Global = 0;
        Table[TabEntry].User = 0;
        Table[TabEntry].Fixed = 0;
        Table[TabEntry].Address = MAX_U32 >> PAGE_SIZE_MUL;

        if (Flags & ALLOC_PAGES_COMMIT) {
            if (Target != 0) {
                Physical = Target + (PHYSICAL)(Index << PAGE_SIZE_MUL);

                if (Flags & ALLOC_PAGES_IO) {
                    Table[TabEntry].Fixed = 1;
                    Table[TabEntry].Present = 1;
                    Table[TabEntry].Privilege = PAGE_PRIVILEGE(Base);
                    Table[TabEntry].Address = Physical >> PAGE_SIZE_MUL;
                } else {
                    SetPhysicalPageMark((UINT)(Physical >> PAGE_SIZE_MUL), 1);
                    Table[TabEntry].Present = 1;
                    Table[TabEntry].Privilege = PAGE_PRIVILEGE(Base);
                    Table[TabEntry].Address = Physical >> PAGE_SIZE_MUL;
                }
            } else {
                Physical = AllocPhysicalPage();

                if (Physical == NULL) {
                    ERROR(TEXT("[%s] AllocPhysicalPage failed"), FunctionName);
                    FreeRegion(RollbackBase, (UINT)(Index << PAGE_SIZE_MUL));
                    return FALSE;
                }

                Table[TabEntry].Present = 1;
                Table[TabEntry].Privilege = PAGE_PRIVILEGE(Base);
                Table[TabEntry].Address = Physical >> PAGE_SIZE_MUL;
            }
        }

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

    // If an exact physical mapping is requested, validate inputs
    if (Target != 0 && (Flags & ALLOC_PAGES_IO) == 0) {
        if ((Target & (PAGE_SIZE - 1)) != 0) {
            ERROR(TEXT("[AllocRegion] Target not page-aligned (%x)"), Target);
            return NULL;
        }

        if ((Flags & ALLOC_PAGES_COMMIT) == 0) {
            ERROR(TEXT("[AllocRegion] Exact PMA mapping requires COMMIT"));
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
 * @brief Unmap and free a linear region.
 * @param Base Base linear address.
 * @param Size Size of region.
 * @return TRUE on success.
 */
BOOL FreeRegion(LINEAR Base, UINT Size) {
    LPPAGE_DIRECTORY Directory = (LPPAGE_DIRECTORY)GetCurrentPageDirectoryVA();
    LPPAGE_TABLE Table = NULL;
    UINT DirEntry = 0;
    UINT TabEntry = 0;
    UINT NumPages = 0;
    UINT Index = 0;

    NumPages = (Size + (PAGE_SIZE - 1)) >> PAGE_SIZE_MUL; /* ceil(Size / 4096) */
    if (NumPages == 0) NumPages = 1;

    // Free each page in turn.
    for (Index = 0; Index < NumPages; Index++) {
        DirEntry = GetDirectoryEntry(Base);
        TabEntry = GetTableEntry(Base);

        if (Directory[DirEntry].Address != NULL) {
            Table = GetPageTableVAFor(Base);

            if (Table[TabEntry].Address != NULL) {
                /* Skip bitmap mark if it was an IO mapping (BAR) */
                if (Table[TabEntry].Fixed == 0) {
                    SetPhysicalPageMark((UINT)Table[TabEntry].Address, 0);
                }

                Table[TabEntry].Present = 0;
                Table[TabEntry].Address = NULL;
                Table[TabEntry].Fixed = 0;
            }
        }

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
    LINEAR result = AlignedResult + PageOffset;
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
    return FreeRegion(LinearBase, Size);
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
    DEBUG(TEXT("[InitializeMemoryManager] Enter"));

    // Place the physical page bitmap at 2MB (half of reserved low memory)
    Kernel_i386.PPB = (LPPAGEBITMAP)LOW_MEMORY_HALF;
    MemorySet(Kernel_i386.PPB, 0, N_128KB);

    MarkUsedPhysicalMemory();

    if (KernelStartup.MemorySize == 0) {
        ConsolePanic(TEXT("Detected memory = 0"));
    }

    // Reserve two temporary linear pages (not committed). They will be remapped on demand.
    G_TempLinear1 = 0xC0100000;  // VA in kernel space, dir=768
    G_TempLinear2 = 0xC0101000;  // next VA, same dir
    G_TempLinear3 = 0xC0102000;  // next VA, same dir

    DEBUG(TEXT("[InitializeMemoryManager] Temp pages reserved: %x, %x, %x"), G_TempLinear1, G_TempLinear2,
        G_TempLinear3);

    // Allocate a page directory
    PHYSICAL NewPageDirectory = AllocPageDirectory();

    LogPageDirectory(NewPageDirectory);

    DEBUG(TEXT("[InitializeMemoryManager] Page directory ready"));

    if (NewPageDirectory == NULL) {
        ERROR(TEXT("[InitializeMemoryManager] AllocPageDirectory failed"));
        ConsolePanic(TEXT("Could not allocate critical memory management tool"));
        DO_THE_SLEEPING_BEAUTY;
    }

    DEBUG(TEXT("[InitializeMemoryManager] New page directory: %x"), NewPageDirectory);

    // Switch to the new page directory first (it includes the recursive map).
    LoadPageDirectory(NewPageDirectory);

    DEBUG(TEXT("[InitializeMemoryManager] Page directory set: %x"), NewPageDirectory);

    // Flush the Translation Look-up Buffer of the CPU
    FlushTLB();

    DEBUG(TEXT("[InitializeMemoryManager] TLB flushed"));

    if (G_TempLinear1 == 0 || G_TempLinear2 == 0) {
        ERROR(TEXT("[InitializeMemoryManager] Failed to reserve temp linear pages"));
        ConsolePanic(TEXT("Could not allocate critical memory management tool"));
        DO_THE_SLEEPING_BEAUTY;
    }

    // Allocate a permanent linear region for the GDT
    Kernel_i386.GDT = (LPSEGMENT_DESCRIPTOR)AllocKernelRegion(0, GDT_SIZE, ALLOC_PAGES_COMMIT | ALLOC_PAGES_READWRITE);

    if (Kernel_i386.GDT == NULL) {
        ERROR(TEXT("[InitializeMemoryManager] AllocRegion for GDT failed"));
        ConsolePanic(TEXT("Could not allocate critical memory management tool"));
        DO_THE_SLEEPING_BEAUTY;
    }

    InitGlobalDescriptorTable(Kernel_i386.GDT);

    DEBUG(TEXT("[InitializeMemoryManager] Loading GDT"));

    LoadGlobalDescriptorTable((PHYSICAL)Kernel_i386.GDT, GDT_SIZE - 1);

    // Log GDT contents
    for (UINT Index = 0; Index < 10; Index++) {
        DEBUG(TEXT("[InitializeMemoryManager] GDT[%u]=%x %x"), Index, ((U32*)(Kernel_i386.GDT))[Index * 2 + 1],
            ((U32*)(Kernel_i386.GDT))[Index * 2]);
    }

    DEBUG(TEXT("[InitializeMemoryManager] Exit"));
}

