# Memory Manager Refactor Plan

## Objectives
- Separate platform-agnostic structures (PPB, temporary mappings, allocation wrappers) from architecture-specific paging logic.
- Reuse proven i386 implementation as reference for the 64-bit rewrite while improving maintainability and speed.
- Deliver clear, robust, and performant implementations for both i386 and x86-64 memory managers.

## File Responsibilities
- `kernel/source/Memory.c`
  - Keep only Physical Page Bitmap (PPB) management.
  - Keep temporary physical page mapping helpers (`MapTemporaryPhysicalPage*`).
  - Keep high-level wrappers such as `AllocKernelRegion` that delegate to architecture-specific backends.
  - Remove any Page Directory (PD) and Page Map Level 4 (PML4) logic.
- `kernel/source/i386-Memory.c`
  - Restore the paging management code from `documentation/internal/code/i386` (main branch reference).
  - Ensure compatibility with the updated shared helpers in `Memory.c`.
- `kernel/source/x86-64-Memory.c`
  - Reimplement paging management for x86-64 without iterators, focusing on predictable and bounded loops.
  - Provide fast lookups for free regions and mappings despite the large virtual address space.
  - Share as much utility logic as possible with i386 through dedicated helper functions.

## Function-Level Plan

### AllocPageDirectory
- **Clarity**: Provide a dedicated helper that zeroes the structure, sets kernel mappings, and documents flag usage.
- **Robustness**: Validate physical page allocation success and handle rollback on failure.
- **Speed**: Cache commonly used kernel PDE templates to avoid repeated setup.

### AllocUserPageDirectory
- **Clarity**: Build on `AllocPageDirectory` while injecting user-specific mappings (user stacks, shared areas).
- **Robustness**: Enforce user/kernel separation checks and clear user-accessible flags appropriately.
- **Speed**: Reuse kernel mapping snapshots and clone only the PDEs required for user space.

### AllocPageTable
- **Clarity**: Abstract PT allocation with explicit parameters for directory entry indexes and flags.
- **Robustness**: Validate PDE availability, clear entries, and maintain reference counts when shared.
- **Speed**: Preallocate contiguous PT blocks when multiple consecutive tables are needed to reduce fragmentation.

### IsRegionFree
- **Clarity**: Split into utility functions per level (PD/PT for i386, PML4/PDPT/PD/PT for x86-64) to avoid nested iterators.
- **Robustness**: Guard against overflow, ensure alignment checks, and verify every level entry is unused.
- **Speed**: Leverage summary bitmaps or cached availability ranges to skip already known occupied sections.

### FindFreeRegion
- **Clarity**: Use a scanning strategy operating on large granularity first (1 GiB / 2 MiB / 4 KiB depending on architecture).
- **Robustness**: Handle wrap-around, alignment requirements, and reserved regions.
- **Speed**: Maintain per-level free span trackers to jump directly to candidate areas without per-page iteration.

### MapLinearToPhysical
- **Clarity**: Split mapping logic per level with helper functions per paging structure.
- **Robustness**: Validate permissions, prevent double-mapping, and update relevant caches.
- **Speed**: Batch map contiguous ranges by installing PT entries in chunks and minimizing TLB flushes.

### AllocRegion
- **Clarity**: Orchestrate `FindFreeRegion`, `MapLinearToPhysical`, and bookkeeping updates in a single, well-documented function.
- **Robustness**: Implement rollback paths when mapping fails mid-way and update all reference counters atomically.
- **Speed**: Prefer large-page allocations when possible and reuse cached free regions.

### ResizeRegion
- **Clarity**: Factor growth vs. shrink paths into separate helpers.
- **Robustness**: Ensure adjacent regions remain intact, handle page table expansion carefully, and update metadata consistently.
- **Speed**: Use differential updates (only touch new or released pages) and avoid full re-scans of existing mappings.

### FreeRegion
- **Clarity**: Decompose into unmap and metadata update stages.
- **Robustness**: Ensure all hierarchical entries are released, handle shared mappings, and zero physical pages if required.
- **Speed**: Release entire tables/directories when empty, avoiding per-page operations.

### MapIOMemory / UnMapIOMemory
- **Clarity**: Provide dedicated helpers for I/O space with explicit cache and permission flags.
- **Robustness**: Enforce alignment constraints and protect against overlapping with standard virtual regions.
- **Speed**: Maintain a registry of active I/O mappings to allow constant-time lookups and removals.

## Implementation Strategy
- Port the known-working i386 implementation first to `i386-Memory.c` from the documented reference.
- Design shared headers and helper functions to minimize duplication between architectures.
- For x86-64, implement multi-level traversal helpers that operate on static-sized arrays or bitmaps instead of iterators.
- Update documentation and tests to reflect the new separation of responsibilities.
