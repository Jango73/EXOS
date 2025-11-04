# x86-64 Virtual Memory Region Optimization Plan

## Objective
- Eliminate the minute-long stall in `FreeRegion` and related slow paths by avoiding per-page table walks.
- Accelerate the entire x86-64 virtual memory management stack (allocation, resize, IO mapping, teardown).
- Preserve correctness for all allocation flags, including large pages and fixed IO mappings.
- Keep i386 logic untouched; only share data structures where safe (e.g., common descriptor declaration).
- Maintain compliance with Base.h types, logging rules, and freestanding constraints.

## Constraints And Observations
- x86-64 code paths (`FindFreeRegion`, `AllocRegion`, `ResizeRegion`, `MapIOMemory`, `FreeRegion`, page table scavengers) iterate 4 KiB at a time, repeatedly resolving the same hierarchy levels.
- Page table walkers flush the TLB once per call, so the dominant cost is the iterator loop, repeated lookups, and bitmap updates.
- RAM bitmap updates happen per page, even when hundreds of consecutive entries share the same state.
- Large mappings are only detected opportunistically, so the kernel still steps through every 4 KiB page when given a 2 MiB or 1 GiB span.
- Region metadata is scattered across allocators; there is no authoritative map of committed regions to drive fast operations.
- i386 paths currently meet performance targets; avoid functional changes there to minimise risk.

## Design Goals
- Handle aligned spans in bulk at each paging level (PML4, PDP, PD, PT) instead of stepping individual entries.
- Apply the same batching strategy across allocation, resize, and unmap flows to eliminate redundant walks.
- Defer bitmap updates to grouped batches to reduce branch mispredictions and cache pollution.
- Introduce lightweight region descriptors so that all x86-64 memory flows start from structured data rather than reconstructing metadata.
- Keep shared definitions (e.g., `MEMORY_REGION_DESCRIPTOR` in `Memory.h`) architecture-neutral while leaving i386 behaviour unchanged.

## Proposed Changes

- Declare `MEMORY_REGION_DESCRIPTOR` in `Memory.h` (shared header) but only integrate it with x86-64 code for now.
- Extend the x86-64 virtual allocator to emit a descriptor (base, size, flags, owner, paging level) on every successful allocation.
- Embed `LISTNODE_FIELDS` in the descriptor so it participates in kernel object tracking and provides the Prev/Next pointers without relying on heap-backed `tag_LIST`.
- Maintain descriptors in an ordered intrusive list per address space with O(log N) lookup by base; implement the list with raw pointer manipulation to avoid the heap.
- Record additional metadata: canonical base, paging granularity (4 KiB / 2 MiB / 1 GiB), fixed/IO status to drive fast decisions.
- Update x86-64 allocation paths (`FindFreeRegion`, `AllocRegion`, `ResizeRegion`, stack builders, `MapIOMemory`) to register and deregister descriptors.

### 2. Fast Walkers (Alloc + Free)
- Implement `ResolveRegionFast` helpers that accept a descriptor and walk the paging hierarchy level-by-level for both allocation and teardown.
- When a descriptor is aligned to a higher-level entry and full-length, operate on the entire entry at once (e.g., mark 512 PT entries committed/free or drop a PD entry for 2 MiB spans).
- Use shared primitives for both `AllocRegion` and `FreeRegion` so the gains apply to allocation, resize, and IO mapping adjustments.
- Only fall back to the legacy iterator for misaligned heads/tails; coalesce the middle span into bulk operations.
- Keep logging minimal (DEBUG level) while preserving the required string format.

### 3. Physical Page Bitmap Batching
- Replace per-page `SetPhysicalPageMark` calls with `SetPhysicalPageMarkRange` or equivalent batching that operates on contiguous runs.
- Invoke the batched API from all x86-64 flows that currently touch the bitmap (allocation, free, resize).
- Guarantee that IO/fixed mappings skip bitmap updates entirely, mirroring existing semantics.
- Validate bitmap updates with instrumentation to ensure no regressions in the RAM allocator.

- Refactor `AllocRegion`, `ResizeRegion`, and `FreeRegion` to resolve descriptors first, then delegate to fast walkers; maintain public signatures.
- Provide helper entry points for kernel stacks and IO mappings so existing callers do not duplicate descriptor lookup logic.
- Keep the legacy iterators behind a debug flag to simplify rollback during validation.
- Avoid any behavioural change to i386 entry points; they continue using existing iterators.
- Allocate descriptor storage from dedicated metadata pages obtained through `AllocPhysicalPage` + temporary mappings; recycle those pages into a free-list of descriptors managed with `LISTNODE_FIELDS`.

### 5. Cross-Architecture Considerations
- Place shared descriptor definitions and lightweight helpers in `Memory.h` / common source, but gate implementation code with `#if defined(ARCH_X86_64)`.
- Ensure i386 builds continue to compile without changes to their behaviour or performance.
- Keep the design portable to simplify a future port, but schedule that work separately once x86-64 stabilizes.

## Implementation Steps
- **Stage 1 (Descriptors):** Introduce descriptor structures in `Memory.h`, integrate with all x86-64 allocation sites, and add diagnostics to confirm correct registration/deregistration.
- **Stage 2 (Fast Walkers):** Implement level-aware walkers for both commit and release, including batched bitmap updates; ensure large-page handling is correct.
- **Stage 3 (Integration):** Swap the x86-64 allocation and free flows to the new path, retain fallback behind a compile-time guard, and verify all kernel unit tests/boot flows.
- **Stage 4 (Cleanup):** Remove temporary instrumentation, update `documentation/internal/Kernel.md`, and document follow-up work for optional i386 adoption without changing its current behaviour.

## Validation Strategy
- Re-run the x86-64 incremental debug build and boot in QEMU; confirm `FreeRegion` no longer dominates teardown time.
- Inspect `log/kernel.log` to verify absence of page faults or leaks during heavy alloc/free workloads.
- Add a regression test in the scheduler stress scenario to repeatedly allocate and free large regions, ensuring performance stays within target.
- Verify that physical memory statistics and RAM bitmap state remain consistent before and after frees.

## Risks And Mitigations
- **Descriptor Drift:** Missing deregistration could leak entries. Mitigate with debug assertions that cross-check the list before freeing.
- **Large Page Coverage Errors:** Clearing the wrong level could free unrelated mappings. Add validation that the descriptor alignment matches the paging level before batching.
- **Concurrency/IRQ Windows:** Ensure descriptor operations respect existing locking/interrupt disable protocols to avoid races.
- **Code Divergence:** Shared logic split between architectures may drift; schedule the i386 adoption promptly after x86-64 validation.
