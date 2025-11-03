# Memory Manager Refactor Plan

## Objectives
- Separate platform-agnostic structures (PPB, temporary mappings, allocation wrappers) from architecture-specific paging logic.
- Reuse proven i386 implementation as reference for the 64-bit write while improving maintainability and speed.
- Deliver clear, robust, and performant implementations for x86-64 memory manager.

## File Responsibilities

### Step 1
- [ ] `kernel/source/Memory.c`
  - Keep Physical Page Bitmap (PPB) management.
  - Keep temporary physical page mapping helpers (`MapTemporaryPhysicalPage*`).
  - Keep high-level wrappers such as `AllocKernelRegion` that delegate to architecture-specific backends.
  - Remove any function with a Page Directory (PD) or Page Map Level 4 (PML4) logic.

### Step 2
- [ ] `kernel/source/i386-Memory.c`
  - Restore the paging management code from `documentation/internal/code/Memory.c` (main branch reference).
  - Ensure compatibility with the shared helpers in `Memory.c`.

### Step 3
- [ ] `kernel/source/x86-64-Memory.c`
  - Implement paging management for x86-64, keeping the same functionnality as i386 but with x86-64 specific strctures.
  - Provide fast lookups for free regions and mappings despite the large virtual address space.
  - Ensure compatibility with the shared helpers in `Memory.c`.
  - WARNING : we are here at the lowest memory management level so it is **forbidden** to call heap management functions.

### Step 4
- [ ] x86-64 AllocPageTable()
  - **Clarity**: Abstract PT allocation with explicit parameters for directory entry indexes and flags.
  - **Robustness**: Validate PDE availability, clear entries, and maintain reference counts when shared.
  - **Speed**: Preallocate contiguous PT blocks when multiple consecutive tables are needed to reduce fragmentation.

### Step 5
- [ ] x86-64 AllocPageDirectory()
  - **Clarity**: Provide a dedicated helper that zeroes the structure, sets kernel mappings, and documents flag usage.
  - **Robustness**: Validate physical page allocation success and handle rollback on failure.
  - **Speed**: Cache commonly used kernel PDE templates to avoid repeated setup.

### Step 6
- [ ] x86-64 AllocUserPageDirectory()
  - **Clarity**: Build on `AllocPageDirectory` while injecting user-specific mappings (user stacks, shared areas).
  - **Robustness**: Enforce user/kernel separation checks and clear user-accessible flags appropriately.
  - **Speed**: Reuse kernel mapping snapshots and clone only the PDEs required for user space.

### Step 7

#### Step 7-A
- [ ] x86-64 IsRegionFree()
  - **Clarity**: Split into utility functions per level (PML4/PDPT/PD/PT) to avoid nested iterators.
  - **Robustness**: Guard against overflow, ensure alignment checks, and verify every level entry is unused.
  - **Speed**: Leverage summary bitmaps or cached availability ranges to skip already known occupied sections.

#### Step 7-B
- [ ] x86-64 FindFreeRegion()
  - **Clarity**: Use a scanning strategy operating on large granularity first (1 GiB / 2 MiB / 4 KiB depending on architecture).
  - **Robustness**: Handle wrap-around, alignment requirements, and reserved regions.
  - **Speed**: Maintain per-level free span trackers to jump directly to candidate areas without per-page iteration.

### Step 8
- [ ] x86-64 MapLinearToPhysical()
  **Clarity**: Split mapping logic per level with helper functions per paging structure.
  **Robustness**: Validate permissions, prevent double-mapping, and update relevant caches.
  **Speed**: Batch map contiguous ranges by installing PT entries in chunks and minimizing TLB flushes.

### Step 9
- [ ] x86-64 AllocRegion()
  - **Clarity**: Orchestrate `FindFreeRegion`, `MapLinearToPhysical`, and bookkeeping updates in a single, well-documented function.
  - **Robustness**: Implement rollback paths when mapping fails mid-way and update all reference counters atomically.
  - **Speed**: Prefer large-page allocations when possible and reuse cached free regions.

### Step 10
- [ ] x86-64 ResizeRegion()
  - **Clarity**: Factor growth vs. shrink paths into separate helpers.
  - **Robustness**: Ensure adjacent regions remain intact, handle page table expansion carefully, and update metadata consistently.
  - **Speed**: Use differential updates (only touch new or released pages) and avoid full re-scans of existing mappings.

### Step 11
- [ ] x86-64 FreeRegion()
  - **Clarity**: Decompose into unmap and metadata update stages.
  - **Robustness**: Ensure all hierarchical entries are released, handle shared mappings, and zero physical pages if required.
  - **Speed**: Release entire tables/directories when empty, avoiding per-page operations.

### Step 12
- [ ] x86-64 MapIOMemory() / UnMapIOMemory()
  - **Clarity**: Provide dedicated helpers for I/O space with explicit cache and permission flags.
  - **Robustness**: Enforce alignment constraints and protect against overlapping with standard virtual regions.
  - **Speed**: Maintain a registry of active I/O mappings to allow constant-time lookups and removals.

## Implementation Strategy
- Design shared headers and helper functions to minimize duplication between architectures, but **DO NOT** mix i386 PD/x86-64 PML4 structures.
- For x86-64, implement multi-level traversal helpers that operate on static-sized arrays or bitmaps instead of iterators.
- Update documentation and tests to reflect the new separation of responsibilities.
