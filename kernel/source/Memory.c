
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

#include "../include/Memory.h"

#include "../include/Base.h"
#include "../include/Console.h"
#include "../include/Kernel.h"
#include "../include/Log.h"
#include "../include/System.h"

extern LINEAR __task_runner_start;
extern LINEAR __task_runner_end;

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
            ┌────────────┬────────────┬────────────┬────────────┬────────────┐
            │     0      │     1      │   ...      │ KernelDir  │   1023     │
            ├────────────┼────────────┼────────────┼────────────┼────────────┤
    points→ │  Low PT    │   PT #1    │   ...      │ Kernel PT  │  SELF-MAP  │
    to PA   │ (0..4MB)   │            │            │ (LA_KERNEL)│ (PD itself)│
            └────────────┴────────────┴────────────┴────────────┴────────────┘
                                                              ^
                                                              │
                                         PDE[1023] -> PD physical page (recursive)
                                                              │
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
       dir = VMA>>22  ───────►  PD_VA[dir] (PDE)  ──────►  PT_VA(dir)[tab] (PTE)  ──────►  PA + ofs

    Kernel mappings installed at init:
    - PDE[0]         -> Low PT (identity map 0..4MB)
    - PDE[KernelDir] -> Kernel PT (maps LA_KERNEL .. LA_KERNEL+4MB-1)
    - PDE[1023]      -> PD itself (self-map)


    Temporary mapping mechanism (MapPhysicalPage):
    1) Two VAs reserved dynamically (e.g., G_TempLinear1, G_TempLinear2).
    2) To map a physical frame P into G_TempLinear1:
       - Compute dir/tab of G_TempLinear1
       - Write the PTE via the PT window:
           PT_VA(dir) = PT_BASE_VA + dir*0x1000, entry [tab]
       - Execute `invlpg [G_TempLinear1]`
       - The physical frame P is now accessible via the VA G_TempLinear1

    Simplified view of the two temporary pages:

                         (reserved via AllocRegion, not present by default)
    G_TempLinear1  ─┐    ┌────────────────────────────────────────────┐
                    ├──► │ PTE ← (Present=1, RW=1, ..., Address=P>>12)│  map/unmap to chosen PA
    G_TempLinear2  ─┘    └────────────────────────────────────────────┘
                                   ^
                                   │ (written through) PT_VA(dir(G_TempLinearX)) = PT_BASE_VA + dir*0x1000
                                   │
                              PD self-map (PD_VA, PT_BASE_VA)

    PDE[1023] points to the Page Directory itself.
    PD_VA = 0xFFFFF000 gives access to the current PD (as PTE-like entries).
    PT_BASE_VA = 0xFFC00000 provides a window for Page Tables:
    PT for directory index D is at PT_BASE_VA + (D * PAGE_SIZE).

    Temporary physical access is done by remapping two reserved
    linear pages (G_TempLinear1, G_TempLinear2) on demand.

\************************************************************************/

// INTERNAL SELF-MAP + TEMP MAPPING ]
/// These are internal-only constants; do not export in public headers.

#define PD_RECURSIVE_SLOT 1023u         /* PDE index used for self-map */
#define PD_VA ((LINEAR)0xFFFFF000)      /* Page Directory linear alias */
#define PT_BASE_VA ((LINEAR)0xFFC00000) /* Page Tables linear window   */

// Two on-demand temporary virtual pages, reserved at init.
static LINEAR G_TempLinear1 = 0;
static LINEAR G_TempLinear2 = 0;

/***************************************************************************/

/**
 * @brief Clip a 64-bit range to 32 bits.
 * @param base Input base address.
 * @param len Length of the range.
 * @param outBase Resulting 32-bit base.
 * @param outLen Resulting 32-bit length.
 * @return Non-zero if clipping succeeded.
 */
static inline int ClipTo32Bit(U64 base, U64 len, U32* outBase, U32* outLen) {
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

/***************************************************************************/

/**
 * @brief Mark a physical page as used or free in the PPB.
 * @param Page Page index.
 * @param Used Non-zero to mark used.
 */
static void SetPhysicalPageMark(U32 Page, U32 Used) {
    U32 Offset = 0;
    U32 Value = 0;

    if (Page >= KernelStartup.PageCount) return;

    LockMutex(MUTEX_MEMORY, INFINITY);

    Offset = Page >> MUL_8;
    Value = (U32)0x01 << (Page & 0x07);

    if (Used) {
        Kernel_i386.PPB[Offset] |= (U8)Value;
    } else {
        Kernel_i386.PPB[Offset] &= (U8)(~Value);
    }

    UnlockMutex(MUTEX_MEMORY);
}

/***************************************************************************/

/**
 * @brief Query the usage mark of a physical page.
 * @param Page Page index.
 * @return Non-zero if page is used.
 */
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

/***************************************************************************/

// Marks a contiguous range of pages in the PPB (1 = used, 0 = free)
/**
 * @brief Mark a range of physical pages as used or free.
 * @param FirstPage First page index.
 * @param PageCount Number of pages.
 * @param Used Non-zero to mark used.
 */
static void SetPhysicalPageRangeMark(U32 FirstPage, U32 PageCount, U32 Used) {
    KernelLogText(LOG_DEBUG, TEXT("[SetPhysicalPageRangeMark] Enter"));

    U32 End = FirstPage + PageCount;
    if (FirstPage >= KernelStartup.PageCount) return;
    if (End > KernelStartup.PageCount) End = KernelStartup.PageCount;

    KernelLogText(LOG_DEBUG, TEXT("[SetPhysicalPageRangeMark] Start, End : %X, %X"), FirstPage, End);

    for (U32 Page = FirstPage; Page < End; Page++) {
        U32 Byte = Page >> MUL_8;
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
    U32 Start = 0;
    U32 End = (N_4MB) >> PAGE_SIZE_MUL;
    SetPhysicalPageRangeMark(Start, End, 1);

    // Derive total memory size and number of pages from the E820 map
    if (KernelStartup.E820_Count > 0) {
        U32 MaxAddress = 0;

        for (U32 i = 0; i < KernelStartup.E820_Count; i++) {
            const E820ENTRY* Entry = &KernelStartup.E820[i];
            U32 Base = 0;
            U32 Size = 0;

            ClipTo32Bit(Entry->Base, Entry->Size, &Base, &Size);

            U32 End = Base + Size;
            if (End > MaxAddress) {
                MaxAddress = End;
            }

            if (Entry->Type != 1) {
                U32 FirstPage = (U32)(Base >> PAGE_SIZE_MUL);
                U32 PageCount = (U32)((Size + PAGE_SIZE - 1) >> PAGE_SIZE_MUL);
                SetPhysicalPageRangeMark(FirstPage, PageCount, 1);
            }
        }

        KernelStartup.MemorySize = (U32)MaxAddress;
        KernelStartup.PageCount = (KernelStartup.MemorySize + (PAGE_SIZE - 1)) >> PAGE_SIZE_MUL;

        KernelLogText(LOG_DEBUG, TEXT("[InitializeMemoryManager] Memory size = %X"), KernelStartup.MemorySize);
    }
}

/************************************************************************/

/* Allocate one free physical page and mark it used in PPB.
   Returns the physical address (page-aligned) or 0 on failure. */
/**
 * @brief Allocate a free physical page.
 * @return Physical page number or MAX_U32 on failure.
 */
PHYSICAL AllocPhysicalPage(void) {
    U32 i, v, bit, page, mask;
    U32 StartPage, StartByte, MaxByte;
    PHYSICAL result = 0;

    // KernelLogText(LOG_DEBUG, TEXT("[AllocPhysicalPage] Enter"));

    LockMutex(MUTEX_MEMORY, INFINITY);

    // Start from end of kernel region
    StartPage = RESERVED_LOW_MEMORY >> PAGE_SIZE_MUL;

    // Convert to PPB byte index
    StartByte = StartPage >> MUL_8; /* == ((... >> 12) >> 3) */
    MaxByte = (KernelStartup.PageCount + 7) >> MUL_8;

    /* Scan from StartByte upward */
    for (i = StartByte; i < MaxByte; i++) {
        v = Kernel_i386.PPB[i];
        if (v != 0xFF) {
            page = (i << MUL_8); /* first page covered by this byte */
            for (bit = 0; bit < 8 && page < KernelStartup.PageCount; bit++, page++) {
                mask = 1u << bit;
                if ((v & mask) == 0) {
                    Kernel_i386.PPB[i] = (U8)(v | mask);
                    result = (PHYSICAL)page << PAGE_SIZE_MUL; /* page * 4096 */
                    goto Out;
                }
            }
        }
    }

Out:
    // KernelLogText(LOG_DEBUG, TEXT("[AllocPhysicalPage] Exit"));

    UnlockMutex(MUTEX_MEMORY);
    return result;
}

/***************************************************************************/

// Frees one physical page and marks it free in the PPB (bitmap).
/**
 * @brief Release a previously allocated physical page.
 * @param Page Page number to free.
 */
void FreePhysicalPage(PHYSICAL Page) {
    U32 StartPage, PageIndex;

    if ((Page & (PAGE_SIZE - 1)) != 0) {
        KernelLogText(LOG_ERROR, TEXT("[FreePhysicalPage] Physical address not page-aligned (%X)"), Page);
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
        KernelLogText(LOG_ERROR, TEXT("[FreePhysicalPage] Attempt to free page 0"));
        return;
    }

    // Bounds check
    if (PageIndex >= KernelStartup.PageCount) {
        KernelLogText(LOG_ERROR, TEXT("[FreePhysicalPage] Page index out of range (%X)"), PageIndex);
        return;
    }

    LockMutex(MUTEX_MEMORY, INFINITY);

    // Bitmap math: 8 pages per byte
    U32 ByteIndex = PageIndex >> MUL_8;        // == PageIndex / 8
    U8 mask = (U8)(1u << (PageIndex & 0x07));  // bit within the byte

    // If already free, nothing to do
    if ((Kernel_i386.PPB[ByteIndex] & mask) == 0) {
        UnlockMutex(MUTEX_MEMORY);
        KernelLogText(LOG_DEBUG, TEXT("[FreePhysicalPage] Page already free (PA=%X)"), Page);
        return;
    }

    // Mark page as free
    Kernel_i386.PPB[ByteIndex] = (U8)(Kernel_i386.PPB[ByteIndex] & (U8)~mask);

    UnlockMutex(MUTEX_MEMORY);
}

/***************************************************************************/

/**
 * @brief Get the page directory index for a linear address.
 * @param Address Linear address.
 * @return Page directory entry index.
 */
static inline U32 GetDirectoryEntry(LINEAR Address) { return Address >> PAGE_TABLE_CAPACITY_MUL; }

/***************************************************************************/

/**
 * @brief Get the page table index for a linear address.
 * @param Address Linear address.
 * @return Page table entry index.
 */
static inline U32 GetTableEntry(LINEAR Address) { return (Address & PAGE_TABLE_CAPACITY_MASK) >> PAGE_SIZE_MUL; }

/***************************************************************************/

// Self-map helpers (no public exposure)

/**
 * @brief Obtain the virtual address of the current page directory.
 * @return Pointer to page directory.
 */
static inline LPPAGEDIRECTORY GetCurrentPageDirectoryVA(void) { return (LPPAGEDIRECTORY)PD_VA; }

/**
 * @brief Get the virtual address of the page table for a linear address.
 * @param Address Linear address.
 * @return Pointer to page table.
 */
static inline LPPAGETABLE GetPageTableVAFor(LINEAR Address) {
    U32 dir = GetDirectoryEntry(Address);
    return (LPPAGETABLE)(PT_BASE_VA + (dir << PAGE_SIZE_MUL));
}

/**
 * @brief Get a pointer to the raw PTE entry for a linear address.
 * @param Address Linear address.
 * @return Pointer to the PTE.
 */
static inline volatile U32* GetPteRawPtr(LINEAR Address) {
    U32 tab = GetTableEntry(Address);
    return (volatile U32*)&GetPageTableVAFor(Address)[tab];
}

// Compose a raw 32-bit PTE value from fields + physical address.
static inline U32 MakePteValue(
    PHYSICAL Physical, U32 ReadWrite, U32 Privilege, U32 WriteThrough, U32 CacheDisabled, U32 Global, U32 Fixed) {
    U32 val = 0;
    val |= 1u;  // Present
    if (ReadWrite) val |= (1u << 1);
    if (Privilege) val |= (1u << 2);  // 1=user, 0=kernel
    if (WriteThrough) val |= (1u << 3);
    if (CacheDisabled) val |= (1u << 4);
    /* Accessed (bit 5) / Dirty (bit 6) left to CPU */
    if (Global) val |= (1u << 8);
    if (Fixed) val |= (1u << 9);                // Your code uses this bit in PTE
    val |= (U32)(Physical & ~(PAGE_SIZE - 1));  // Frame address aligned
    return val;
}

// Map or remap a single virtual page by directly editing its PTE via the self-map.
static inline void MapOnePage(
    LINEAR Linear, PHYSICAL Physical, U32 ReadWrite, U32 Privilege, U32 WriteThrough, U32 CacheDisabled, U32 Global,
    U32 Fixed) {
    volatile U32* Pte = GetPteRawPtr(Linear);
    LPPAGEDIRECTORY Directory = GetCurrentPageDirectoryVA();
    U32 dir = GetDirectoryEntry(Linear);
    if (!Directory[dir].Present) {
        KernelLogText(LOG_ERROR, TEXT("[MapOnePage] PDE not present for VA %X (dir=%d)"), Linear, dir);
        return;  // Ou panic, selon la politique
    }
    *Pte = MakePteValue(Physical, ReadWrite, Privilege, WriteThrough, CacheDisabled, Global, Fixed);
    InvalidatePage(Linear);
}

// Unmap (mark not-present) one virtual page.
/**
 * @brief Unmap a single page from the current address space.
 * @param Linear Linear address to unmap.
 */
static inline void UnmapOnePage(LINEAR Linear) {
    volatile U32* Pte = GetPteRawPtr(Linear);
    *Pte = 0;
    InvalidatePage(Linear);
}

/***************************************************************************/

/**
 * @brief Check if a linear address is mapped and accessible.
 * @param Pointer Linear address to test.
 * @return TRUE if address is valid.
 */
BOOL IsValidMemory(LINEAR Pointer) {
    LPPAGEDIRECTORY Directory = GetCurrentPageDirectoryVA();

    U32 dir = GetDirectoryEntry(Pointer);
    U32 tab = GetTableEntry(Pointer);

    /* Bounds check (defensive) */
    if (dir >= PAGE_TABLE_NUM_ENTRIES) return FALSE;
    if (tab >= PAGE_TABLE_NUM_ENTRIES) return FALSE;

    /* Page directory present? */
    if (Directory[dir].Present == 0) return FALSE;

    /* Page table present? */
    LPPAGETABLE Table = GetPageTableVAFor(Pointer);
    if (Table[tab].Present == 0) return FALSE;

    return TRUE;
}

/*************************************************************************/

// Public temporary map #1
/**
 * @brief Map a physical page to a temporary linear address.
 * @param Physical Physical page number.
 * @return Linear address mapping or 0 on failure.
 */
LINEAR MapPhysicalPage(PHYSICAL Physical) {
    if (G_TempLinear1 == 0) {
        KernelLogText(LOG_ERROR, TEXT("[MapPhysicalPage] Temp slot #1 not reserved"));
        return NULL;
    }
    MapOnePage(
        G_TempLinear1, Physical,
        /*RW*/ 1, PAGE_PRIVILEGE_KERNEL, /*WT*/ 0, /*UC*/ 0, /*Global*/ 0, /*Fixed*/ 1);
    return G_TempLinear1;
}

// Internal temporary map #2
/**
 * @brief Map a physical page to the second temporary linear address.
 * @param Physical Physical page number.
 * @return Linear address mapping or 0 on failure.
 */
static LINEAR MapPhysicalPage2(PHYSICAL Physical) {
    if (G_TempLinear2 == 0) {
        KernelLogText(LOG_ERROR, TEXT("[MapPhysicalPage2] Temp slot #2 not reserved"));
        return NULL;
    }
    MapOnePage(
        G_TempLinear2, Physical,
        /*RW*/ 1, PAGE_PRIVILEGE_KERNEL, /*WT*/ 0, /*UC*/ 0, /*Global*/ 0, /*Fixed*/ 1);
    return G_TempLinear2;
}

/************************************************************************/
/*
 * AllocPageDirectory
 * - Identity-map the first 4MB at 0x00000000..0x003FFFFF
 * - Map the kernel at LA_KERNEL to KernelStartup.StubAddress (4MB window)
 * - Install recursive mapping PDE[1023] = PD
 *
 * @return: The physical address of the page directory that goes in cr3
 */

/**
 * @brief Allocate a new page directory.
 * @return Physical address of the page directory or MAX_U32 on failure.
 */
PHYSICAL AllocPageDirectory(void) {
    PHYSICAL PA_Directory = NULL;
    PHYSICAL PA_LowTable = NULL;
    PHYSICAL PA_KernelTable = NULL;

    LPPAGEDIRECTORY Directory = NULL;
    LPPAGETABLE LowTable = NULL;
    LPPAGETABLE KernelTable = NULL;

    KernelLogText(LOG_DEBUG, TEXT("[AllocPageDirectory] Enter"));

    U32 DirKernel = (LA_KERNEL >> PAGE_TABLE_CAPACITY_MUL);  // 4MB directory slot for LA_KERNEL
    U32 PhysBaseKernel = KernelStartup.StubAddress;          // Kernel physical base
    U32 Index;

    // Allocate required physical pages (PD + 2 PTs)
    PA_Directory = AllocPhysicalPage();
    PA_LowTable = AllocPhysicalPage();
    PA_KernelTable = AllocPhysicalPage();

    if (PA_Directory == NULL || PA_LowTable == NULL || PA_KernelTable == NULL) {
        KernelLogText(LOG_ERROR, TEXT("[AllocPageDirectory] Out of physical pages"));
        goto Out_Error;
    }

    // Clear and prepare the Page Directory
    LINEAR LA_PD = MapPhysicalPage(PA_Directory);
    if (LA_PD == NULL) {
        KernelLogText(LOG_ERROR, TEXT("[AllocPageDirectory] MapPhysicalPage failed on Directory"));
        goto Out_Error;
    }
    Directory = (LPPAGEDIRECTORY)LA_PD;
    MemorySet(Directory, 0, PAGE_SIZE);

    KernelLogText(LOG_DEBUG, TEXT("[AllocPageDirectory] Page directory cleared"));

    // Directory[0] -> identity map 0..4MB via PA_LowTable
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
    Directory[0].Address = (PA_LowTable >> PAGE_SIZE_MUL);

    // Directory[DirKernel] -> map LA_KERNEL..LA_KERNEL+4MB-1 to KERNEL_PHYSICAL_ORIGIN..+4MB-1
    Directory[DirKernel].Present = 1;
    Directory[DirKernel].ReadWrite = 1;
    Directory[DirKernel].Privilege = PAGE_PRIVILEGE_USER;  // Allow user stub access
    Directory[DirKernel].WriteThrough = 0;
    Directory[DirKernel].CacheDisabled = 0;
    Directory[DirKernel].Accessed = 0;
    Directory[DirKernel].Reserved = 0;
    Directory[DirKernel].PageSize = 0;  // 4KB pages
    Directory[DirKernel].Global = 0;
    Directory[DirKernel].User = 0;
    Directory[DirKernel].Fixed = 1;
    Directory[DirKernel].Address = (PA_KernelTable >> PAGE_SIZE_MUL);

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
    Directory[PD_RECURSIVE_SLOT].Address = (PA_Directory >> PAGE_SIZE_MUL);

    // Fill identity-mapped low table (0..4MB)
    LINEAR LA_PT = MapPhysicalPage2(PA_LowTable);
    if (LA_PT == NULL) {
        KernelLogText(LOG_ERROR, TEXT("[AllocPageDirectory] MapPhysicalPage2 failed on LowTable"));
        goto Out_Error;
    }
    LowTable = (LPPAGETABLE)LA_PT;
    MemorySet(LowTable, 0, PAGE_SIZE);

    KernelLogText(LOG_DEBUG, TEXT("[AllocPageDirectory] Low memory table cleared"));

    for (Index = 0; Index < PAGE_TABLE_NUM_ENTRIES; Index++) {
        LowTable[Index].Present = 1;
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

    // Fill kernel mapping table
    LA_PT = MapPhysicalPage2(PA_KernelTable);
    if (LA_PT == NULL) {
        KernelLogText(LOG_ERROR, TEXT("[AllocPageDirectory] MapPhysicalPage2 failed on KernelTable"));
        goto Out_Error;
    }
    KernelTable = (LPPAGETABLE)LA_PT;
    MemorySet(KernelTable, 0, PAGE_SIZE);

    KernelLogText(LOG_DEBUG, TEXT("[AllocPageDirectory] Kernel table cleared"));

    U32 KernelFirstFrame = (PhysBaseKernel >> PAGE_SIZE_MUL);
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

    // TLB sync before returning
    FlushTLB();

    KernelLogText(
        LOG_DEBUG, TEXT("[AllocPageDirectory] PDE[0]=%X, PDE[768]=%X, PDE[1023]=%X"), *(U32*)&Directory[0],
        *(U32*)&Directory[768], *(U32*)&Directory[1023]);
    KernelLogText(
        LOG_DEBUG, TEXT("[AllocPageDirectory] LowTable[0]=%X, KernelTable[0]=%X"), *(U32*)&LowTable[0],
        *(U32*)&KernelTable[0]);

    KernelLogText(LOG_DEBUG, TEXT("[AllocPageDirectory] Exit"));
    return PA_Directory;

Out_Error:

    if (PA_Directory) FreePhysicalPage(PA_Directory);
    if (PA_LowTable) FreePhysicalPage(PA_LowTable);
    if (PA_KernelTable) FreePhysicalPage(PA_KernelTable);

    return NULL;
}

/************************************************************************/
/*
 * AllocPageTable
 * AllocPageTable expands the page tables of the calling process by
 * allocating a page table to map the "Base" linear address.
 *
 * @return: The virtual address of the new page
 */

/**
 * @brief Allocate a page table for the given base address.
 * @param Base Base linear address.
 * @return Linear address of the new table or 0.
 */
LINEAR AllocPageTable(LINEAR Base) {
    PHYSICAL PA_Table = AllocPhysicalPage();

    if (PA_Table == NULL) {
        KernelLogText(LOG_ERROR, TEXT("[AllocPageTable] Out of physical pages"));
        return NULL;
    }

    // Fill the directory entry that describes the new table
    U32 DirEntry = GetDirectoryEntry(Base);
    LPPAGEDIRECTORY Directory = GetCurrentPageDirectoryVA();

    Directory[DirEntry].Present = 1;
    Directory[DirEntry].ReadWrite = 1;
    Directory[DirEntry].Privilege = PAGE_PRIVILEGE_KERNEL;
    Directory[DirEntry].WriteThrough = 0;
    Directory[DirEntry].CacheDisabled = 0;
    Directory[DirEntry].Accessed = 0;
    Directory[DirEntry].Reserved = 0;
    Directory[DirEntry].PageSize = 0;
    Directory[DirEntry].Global = 0;
    Directory[DirEntry].User = 0;
    Directory[DirEntry].Fixed = 1;
    Directory[DirEntry].Address = PA_Table >> PAGE_SIZE_MUL;

    // Clear the new table by mapping its physical page temporarily.
    LINEAR LA_PT = MapPhysicalPage2(PA_Table);
    MemorySet((LPVOID)LA_PT, 0, PAGE_SIZE);

    // Flush the Translation Look-up Buffer of the CPU
    FlushTLB();

    // Return the linear address of the table via the recursive window
    return (LINEAR)GetPageTableVAFor(Base);
}

/************************************************************************/
/*
 * IsRegionFree
 * Checks if a specified memory region is free
 *
 * @Base: The base of the region to check
 * @Size: The size of the region to check
 *
 * @return: TRUE if the region is free, FALSE otherwise
 */

/**
 * @brief Check if a linear region is free of mappings.
 * @param Base Base linear address.
 * @param Size Size of region.
 * @return TRUE if region is free.
 */
BOOL IsRegionFree(LINEAR Base, U32 Size) {
    // KernelLogText(LOG_DEBUG, TEXT("[IsRegionFree] Enter"));

    U32 NumPages = (Size + PAGE_SIZE - 1) >> PAGE_SIZE_MUL;
    LPPAGEDIRECTORY Directory = GetCurrentPageDirectoryVA();
    LINEAR Current = Base;

    // KernelLogText(LOG_DEBUG, TEXT("[IsRegionFree] Traversing pages"));

    for (U32 i = 0; i < NumPages; i++) {
        U32 dir = GetDirectoryEntry(Current);
        U32 tab = GetTableEntry(Current);

        if (Directory[dir].Present) {
            LPPAGETABLE Table = GetPageTableVAFor(Current);
            if (Table[tab].Present) return FALSE;
        }

        Current += PAGE_SIZE;
    }

    // KernelLogText(LOG_DEBUG, TEXT("[IsRegionFree] Exit"));

    return TRUE;
}

/*************************************************************************/
/* Tries to find a linear address region of "Size" bytes
   which is not mapped in the page tables */

/**
 * @brief Find a free linear region starting from a base address.
 * @param StartBase Starting linear address.
 * @param Size Desired region size.
 * @return Base of free region or 0.
 */
static LINEAR FindFreeRegion(U32 StartBase, U32 Size) {
    U32 Base = N_4MB;

    KernelLogText(LOG_DEBUG, TEXT("[FindFreeRegion] Enter"));

    if (StartBase >= N_4MB) {
        KernelLogText(LOG_DEBUG, TEXT("[FindFreeRegion] Starting at %X"), StartBase);
        Base = StartBase;
    }

    while (1) {
        if (IsRegionFree(Base, Size) == TRUE) return Base;
        Base += PAGE_SIZE;
    }

    KernelLogText(LOG_DEBUG, TEXT("[FindFreeRegion] Exit"));

    return NULL;
}

/*************************************************************************/
// Checks if a physical memory range is free

/**
 * @brief Check if a range of physical pages is free.
 * @param Target Starting physical page.
 * @param NumPages Number of pages.
 * @return TRUE if range is free.
 */
static BOOL IsPhysicalRangeFree(PHYSICAL Target, U32 NumPages) {
    U32 PageIndex = 0;

    if ((Target & (PAGE_SIZE - 1)) != 0) return FALSE;
    for (PageIndex = 0; PageIndex < NumPages; PageIndex++) {
        U32 Page = (Target >> PAGE_SIZE_MUL) + PageIndex;
        if (GetPhysicalPageMark(Page)) return FALSE;
    }
    return TRUE;
}

/*************************************************************************/

/**
 * @brief Release page tables that no longer contain mappings.
 */
static void FreeEmptyPageTables(void) {
    LPPAGEDIRECTORY Directory = GetCurrentPageDirectoryVA();
    LPPAGETABLE Table = NULL;
    LINEAR Base = N_4MB;
    U32 DirEntry = 0;
    U32 Index = 0;
    U32 DestroyIt = 0;

    while (Base < LA_KERNEL) {
        DestroyIt = 1;
        DirEntry = GetDirectoryEntry(Base);

        if (Directory[DirEntry].Address != NULL) {
            Table = GetPageTableVAFor(Base);

            for (Index = 0; Index < PAGE_TABLE_NUM_ENTRIES; Index++) {
                if (Table[Index].Address != NULL) DestroyIt = 0;
            }

            if (DestroyIt) {
                SetPhysicalPageMark(Directory[DirEntry].Address, 0);
                Directory[DirEntry].Present = 0;
                Directory[DirEntry].Address = NULL;
            }
        }

        Base += PAGE_TABLE_CAPACITY;
    }
}

/*************************************************************************/
/* Maps a linear address to its physical address (page-level granularity).
   Returns 0 on failure. */

/**
 * @brief Translate a linear address to its physical counterpart.
 * @param Address Linear address.
 * @return Physical page number or MAX_U32 on failure.
 */
PHYSICAL MapLinearToPhysical(LINEAR Address) {
    LPPAGEDIRECTORY Directory = GetCurrentPageDirectoryVA();
    U32 DirEntry = GetDirectoryEntry(Address);
    U32 TabEntry = GetTableEntry(Address);

    if (Directory[DirEntry].Address == 0) return 0;

    LPPAGETABLE Table = GetPageTableVAFor(Address);
    if (Table[TabEntry].Address == 0) return 0;

    /* Compose physical: page frame << 12 | offset-in-page */
    return (PHYSICAL)((Table[TabEntry].Address << PAGE_SIZE_MUL) | (Address & (PAGE_SIZE - 1)));
}

/***************************************************************************\

  AllocRegion is the most important memory management function.
  It allocates a linear address memory region to the calling process
  and sets up the page tables.

  If the user supplies a linear address as a base for the region,
  AllocRegion returns NULL if the region is already allocated.

  If the user supplies a physical target address, AllocRegion returns
  NULL if the physical region is not free.

  The pages can be physically allocated if the flags include
  ALLOC_PAGES_COMMIT or can be reserved (not present in physical
  memory) with the ALLOC_PAGES_RESERVE flag.

\***************************************************************************/

/**
 * @brief Map a physical region into the linear address space.
 * @param Base Desired base address or 0.
 * @param Target Physical base address.
 * @param Size Size in bytes.
 * @param Flags Mapping flags.
 * @return Linear base address or 0 on failure.
 */
LINEAR AllocRegion(LINEAR Base, PHYSICAL Target, U32 Size, U32 Flags) {
    LPPAGEDIRECTORY Directory = GetCurrentPageDirectoryVA();
    LPPAGETABLE Table = NULL;
    LINEAR Pointer = NULL;
    PHYSICAL Physical = NULL;
    U32 DirEntry = 0;
    U32 TabEntry = 0;
    U32 NumPages = 0;
    U32 ReadWrite = 0;
    U32 Privilege = 0;
    U32 Index = 0;

    KernelLogText(LOG_DEBUG, TEXT("[AllocRegion] Enter"));

    // Can't allocate more than 25% of total memory at once
    if (Size > KernelStartup.MemorySize / 4) {
        return NULL;
    }

    // Rounding behavior for page count
    NumPages = (Size + (PAGE_SIZE - 1)) >> PAGE_SIZE_MUL;  // ceil(Size / 4096)
    if (NumPages == 0) NumPages = 1;

    ReadWrite = (Flags & ALLOC_PAGES_READWRITE) ? 1 : 0;
    Privilege = PAGE_PRIVILEGE_USER;

    // Derive cache policy flags for PTE
    U32 PteCacheDisabled = (Flags & ALLOC_PAGES_UC) ? 1 : 0;
    U32 PteWriteThrough = (Flags & ALLOC_PAGES_WC) ? 1 : 0;

    // If UC is set, it dominates; WT must be 0 in that case.
    if (PteCacheDisabled) PteWriteThrough = 0;

    // If an exact physical mapping is requested, validate inputs
    if (Target != 0 && (Flags & ALLOC_PAGES_IO) == 0) {
        if ((Target & (PAGE_SIZE - 1)) != 0) {
            KernelLogText(LOG_ERROR, TEXT("[AllocRegion] Target not page-aligned (%X)"), Target);
            return NULL;
        }
        if ((Flags & ALLOC_PAGES_COMMIT) == 0) {
            KernelLogText(LOG_ERROR, TEXT("[AllocRegion] Exact PMA mapping requires COMMIT"));
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
            KernelLogText(LOG_DEBUG, TEXT("[AllocRegion] No free region found with specified base"));
            return NULL;
        }
    }

    /* If the calling process does not care about the base address of
       the region, try to find a region which is at least as large as
       the "Size" parameter. */
    if (Base == 0 || (Flags & ALLOC_PAGES_AT_OR_OVER)) {
        KernelLogText(LOG_DEBUG, TEXT("[AllocRegion] Calling FindFreeRegion with base = %X and size = %X"), Base, Size);

        LINEAR NewBase = FindFreeRegion(Base, Size);

        if (NewBase == NULL) {
            KernelLogText(LOG_DEBUG, TEXT("[AllocRegion] No free region found with unspecified base from %X"), Base);
            return NULL;
        }

        Base = NewBase;
    }

    // Set the return value to "Base".
    Pointer = Base;

    KernelLogText(LOG_DEBUG, TEXT("[AllocRegion] Allocating pages"));

    /* Allocate each page in turn. */
    for (Index = 0; Index < NumPages; Index++) {
        DirEntry = GetDirectoryEntry(Base);
        TabEntry = GetTableEntry(Base);

        if (Directory[DirEntry].Address == NULL) {
            if (AllocPageTable(Base) == NULL) {
                FreeRegion(Pointer, (Index << PAGE_SIZE_MUL));
                KernelLogText(LOG_DEBUG, TEXT("[AllocRegion] AllocPageTable failed "));
                return NULL;
            }
        }

        Table = GetPageTableVAFor(Base);

        Table[TabEntry].Present = 0;
        Table[TabEntry].ReadWrite = ReadWrite;
        Table[TabEntry].Privilege = Privilege;
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
                Physical = Target + (Index << PAGE_SIZE_MUL);

                if (Flags & ALLOC_PAGES_IO) {
                    /* IO mapping (BAR) -> no bitmap mark, Fixed=1 */
                    Table[TabEntry].Fixed = 1;
                    Table[TabEntry].Present = 1;
                    Table[TabEntry].Address = Physical >> PAGE_SIZE_MUL;
                } else {
                    /* RAM mapping */
                    SetPhysicalPageMark(Physical >> PAGE_SIZE_MUL, 1);
                    Table[TabEntry].Present = 1;
                    Table[TabEntry].Address = Physical >> PAGE_SIZE_MUL;
                }
            } else {
                /* Legacy path: allocate any free physical page */
                Physical = AllocPhysicalPage();

                if (Physical == NULL) {
                    KernelLogText(LOG_ERROR, TEXT("[AllocRegion] AllocPhysicalPage failed"));
                    /* Roll back pages mapped so far */
                    FreeRegion(Pointer, (Index << PAGE_SIZE_MUL));
                    return NULL;
                }

                Table[TabEntry].Present = 1;
                Table[TabEntry].Address = Physical >> PAGE_SIZE_MUL;
            }
        }

        // Advance to next page
        Base += PAGE_SIZE;
    }

    // Flush the Translation Look-up Buffer of the CPU
    FlushTLB();

    KernelLogText(LOG_DEBUG, TEXT("[AllocRegion] Exit"));

    return Pointer;
}

/***************************************************************************/

/**
 * @brief Unmap and free a linear region.
 * @param Base Base linear address.
 * @param Size Size of region.
 * @return TRUE on success.
 */
BOOL FreeRegion(LINEAR Base, U32 Size) {
    LPPAGEDIRECTORY Directory = (LPPAGEDIRECTORY)GetCurrentPageDirectoryVA();
    LPPAGETABLE Table = NULL;
    U32 DirEntry = 0;
    U32 TabEntry = 0;
    U32 NumPages = 0;
    U32 Index = 0;

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
                    SetPhysicalPageMark(Table[TabEntry].Address, 0);
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

/***************************************************************************\

  PCI BAR mapping process (example: Intel E1000 NIC)

  ┌───────────────────────────┐
  │  PCI Configuration Space  │
  │  (accessed via PCI config │
  │   reads/writes)           │
  └─────────────┬─────────────┘
                │
                │ Read BAR0 (Base Address Register #0)
                ▼
       ┌────────────────────────────────┐
       │ BAR0 value = Physical address  │
       │ of device registers (MMIO)     │
       │ + resource size                │
       └─────────────┬──────────────────┘
                     │
                     │ Map physical MMIO region into
                     │ kernel virtual space
                     │ (uncached for DMA safety)
                     │
                     ▼
         ┌───────────────────────────┐
         │ AllocRegion(Base=0,       │
         │   Target=BAR0,            │
         │   Size=MMIO size,         │
         │   Flags=ALLOC_PAGES_COMMIT│
         │         | ALLOC_PAGES_UC) │
         └─────────┬─────────────────┘
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

\***************************************************************************/

/**
 * @brief Map an I/O physical range into virtual memory.
 * @param PhysicalBase Physical base address.
 * @param Size Size in bytes.
 * @return Linear address or 0 on failure.
 */
LINEAR MmMapIo(PHYSICAL PhysicalBase, U32 Size) {
    // Basic parameter checks
    if (PhysicalBase == 0 || Size == 0) {
        KernelLogText(LOG_ERROR, TEXT("[MmMapIo] Invalid parameters (PA=%X Size=%X)"), PhysicalBase, Size);
        return NULL;
    }

    if ((PhysicalBase & (PAGE_SIZE - 1)) != 0) {
        KernelLogText(LOG_ERROR, TEXT("[MmMapIo] Physical base not page-aligned (%X)"), PhysicalBase);
        return NULL;
    }

    // Map as Uncached, Read/Write, exact PMA mapping, IO semantics
    return AllocRegion(
        0,             // Let AllocRegion choose virtual address
        PhysicalBase,  // Exact PMA (BAR)
        Size,
        ALLOC_PAGES_COMMIT | ALLOC_PAGES_READWRITE | ALLOC_PAGES_UC |  // MMIO must be UC
            ALLOC_PAGES_IO                                             // Do not touch RAM bitmap; mark PTE.Fixed
    );
}

/***************************************************************************/

/**
 * @brief Unmap a previously mapped I/O range.
 * @param LinearBase Linear base address.
 * @param Size Size in bytes.
 * @return TRUE on success.
 */
BOOL MmUnmapIo(LINEAR LinearBase, U32 Size) {
    // Basic parameter checks
    if (LinearBase == 0 || Size == 0) {
        KernelLogText(LOG_ERROR, TEXT("[MmUnmapIo] Invalid parameters (LA=%X Size=%X)"), LinearBase, Size);
        return FALSE;
    }

    // Just unmap; FreeRegion will skip RAM bitmap if PTE.Fixed was set
    return FreeRegion(LinearBase, Size);
}

/***************************************************************************/

/**
 * @brief Initialize the kernel memory manager.
 */
void InitializeMemoryManager(void) {
    KernelLogText(LOG_DEBUG, TEXT("[InitializeMemoryManager] Enter"));

    // Place the physical page bitmap at 2MB (half of reserved low memory)
    Kernel_i386.PPB = (LPPAGEBITMAP)LOW_MEMORY_HALF;
    MemorySet(Kernel_i386.PPB, 0, N_128KB);

    MarkUsedPhysicalMemory();

    if (KernelStartup.MemorySize == 0) {
        ConsolePanic(TEXT("Detected memory = 0"));
    }

    // Reserve two temporary linear pages (not committed). They will be remapped on demand.
    G_TempLinear1 = 0xC0100000;  // VA dans kernel space, dir=768
    G_TempLinear2 = 0xC0101000;  // VA suivante, même dir

    KernelLogText(
        LOG_DEBUG, TEXT("[InitializeMemoryManager] Temp pages reserved: %X and %X"), G_TempLinear1, G_TempLinear2);

    // Reserve VAs via AllocRegion without commit
    if (!AllocRegion(G_TempLinear1, 0, PAGE_SIZE, ALLOC_PAGES_RESERVE | ALLOC_PAGES_READWRITE)) {
        KernelLogText(LOG_ERROR, TEXT("[InitializeMemoryManager] Failed to reserve G_TempLinear1"));
        ConsolePanic(TEXT("Could not allocate critical memory management tool"));
        DO_THE_SLEEPING_BEAUTY;
    }

    if (!AllocRegion(G_TempLinear2, 0, PAGE_SIZE, ALLOC_PAGES_RESERVE | ALLOC_PAGES_READWRITE)) {
        KernelLogText(LOG_ERROR, TEXT("[InitializeMemoryManager] Failed to reserve G_TempLinear2"));
        ConsolePanic(TEXT("Could not allocate critical memory management tool"));
        DO_THE_SLEEPING_BEAUTY;
    }

    // Allocate a page directory
    PHYSICAL NewPageDirectory = AllocPageDirectory();

    KernelLogText(LOG_DEBUG, TEXT("[InitializeMemoryManager] Page directory ready"));

    if (NewPageDirectory == NULL) {
        KernelLogText(LOG_ERROR, TEXT("[InitializeMemoryManager] AllocPageDirectory failed"));
        ConsolePanic(TEXT("Could not allocate critical memory management tool"));
        DO_THE_SLEEPING_BEAUTY;
    }

    KernelLogText(LOG_DEBUG, TEXT("[InitializeMemoryManager] New page directory: %X"), NewPageDirectory);

    // Switch to the new page directory first (it includes the recursive map).
    SetPageDirectory(NewPageDirectory);

    KernelLogText(LOG_DEBUG, TEXT("[InitializeMemoryManager] Page directory set: %X"), NewPageDirectory);

    // Flush the Translation Look-up Buffer of the CPU
    FlushTLB();

    KernelLogText(LOG_DEBUG, TEXT("[InitializeMemoryManager] TLB flushed"));

    if (G_TempLinear1 == 0 || G_TempLinear2 == 0) {
        KernelLogText(LOG_ERROR, TEXT("[InitializeMemoryManager] Failed to reserve temp linear pages"));
        ConsolePanic(TEXT("Could not allocate critical memory management tool"));
        DO_THE_SLEEPING_BEAUTY;
    }

    // Allocate a permanent linear region for the GDT
    Kernel_i386.GDT = (LPSEGMENTDESCRIPTOR)AllocRegion(
        LA_KERNEL, 0, GDT_SIZE, ALLOC_PAGES_COMMIT | ALLOC_PAGES_READWRITE | ALLOC_PAGES_AT_OR_OVER);

    if (Kernel_i386.GDT == NULL) {
        KernelLogText(LOG_ERROR, TEXT("[InitializeMemoryManager] AllocRegion for GDT failed"));
        ConsolePanic(TEXT("Could not allocate critical memory management tool"));
        DO_THE_SLEEPING_BEAUTY;
    }

    InitGlobalDescriptorTable(Kernel_i386.GDT);

    KernelLogText(LOG_DEBUG, TEXT("[InitializeMemoryManager] Loading GDT"));

    LoadGlobalDescriptorTable((PHYSICAL)Kernel_i386.GDT, GDT_SIZE - 1);

    // Log GDT contents
    for (U32 i = 0; i < 3; i++) {
        KernelLogText(
            LOG_DEBUG, TEXT("[InitializeMemoryManager] GDT[%u]=0x%X%X"), i, ((U32*)(Kernel_i386.GDT))[i * 2 + 1],
            ((U32*)(Kernel_i386.GDT))[i * 2]);
    }

    // Expose TaskRunner section to user mode
    {
        LINEAR TaskStubStart = (LINEAR)(&__task_runner_start);
        LINEAR TaskStubEnd = (LINEAR)(&__task_runner_end);

        for (LINEAR Address = TaskStubStart & ~(PAGE_SIZE - 1); Address < TaskStubEnd; Address += PAGE_SIZE) {
            volatile U32* Pte = GetPteRawPtr(Address);
            *Pte |= (1u << 2);  // Set user privilege bit
            InvalidatePage(Address);
        }

        KernelLogText(LOG_DEBUG, TEXT("[InitializeMemoryManager] TaskRunner marked user"));
    }

    KernelLogText(LOG_DEBUG, TEXT("[InitializeMemoryManager] Exit"));
}

/***************************************************************************/
