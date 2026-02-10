# Buddy Allocator Refactor Plan (x86-32 + x86-64)

## Goals
- Replace physical page bitmap allocation with a buddy allocator.
- Remove x86-64 descriptor-based fast VMM path (`x86-64-Memory-Descriptors`).
- Keep one buddy allocator module usable by both architectures.

## Step 1 - Common Buddy Allocator Module
- [X] Add a shared buddy allocator module (header + source) under `kernel/include` and `kernel/source`.
- [X] Use architecture-neutral data structures, preferring 64-bit safe arithmetic.
- [X] Implement initialization API for physical page management from multiboot memory map.
- [X] Implement single-page allocate/free APIs used by `AllocPhysicalPage` and `FreePhysicalPage`.
- [X] Implement range mark APIs to preserve existing callers (`SetPhysicalPageMark`, `SetPhysicalPageRangeMark`).
- [X] Add early-boot validation/logging for allocator geometry and reserved ranges.

## Step 2 - Wire Into x86-32 Memory Manager
- [X] Replace x86-32 bitmap bootstrap setup with buddy bootstrap setup.
- [X] Keep loader-reserved and low-memory protections equivalent to previous behavior.
- [X] Ensure x86-32 `InitializeMemoryManager` uses buddy APIs before first page-directory allocation.
- [X] Verify `AllocRegion`/`FreeRegion` physical page ownership remains consistent.
- [X] Build-check x86-32 kernel.

## Step 3 - Wire Into x86-64 Memory Manager
- [X] Replace x86-64 bitmap bootstrap setup with buddy bootstrap setup.
- [X] Remove x86-64 descriptor fast-walker source file and integration points.
- [X] Keep working legacy page population/free behavior in x86-64 without descriptor fast path.
- [X] Ensure region tracking (`Memory-Descriptors.c`) still works for allocation bookkeeping.
- [X] Build-check x86-64 kernel.

## Cleanup and Documentation
- [X] Remove dead bitmap-specific code paths no longer needed for physical page allocation.
- [X] Update `documentation/Kernel.md` to reflect buddy allocator physical memory management.
- [X] Re-check plan checklist and mark all completed items.
