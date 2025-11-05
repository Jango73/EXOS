# Handle Pointer Masking Implementation Plan

## Objectives
- Introduce kernel-side pointer masking by exposing userland handles instead of raw pointers for both i386 and x86-64 builds.
- Provide an efficient, freestanding mapping layer that respects EXOS coding standards (Base.h types, SAFE_USE macros, custom logging, no external libraries).
- Ensure every syscall resolves handles back to kernel pointers without regressing existing behaviour; pointer-to-handle lookups will be delegated to subsystems that allocate the handles.

## Architectural Overview
- `handle` values are `U32`, start at `0`, and auto-increment on allocation; rollover handling must trap on overflow to keep behaviour deterministic.
- A central handle dictionary lives in `Kernel` (see `kernel/include/Kernel.h`) and is shared across architectures via `KERNELDATA`.
- Kernel code manipulates concrete pointers; userland-visible interfaces exchange handles. Conversions happen at syscall entry/exit boundaries in `kernel/source/SYSCall.c`.

## Stage 1 - BlockList Allocator
- Implement a lightweight allocator named `BlockList` that mimics the Linux SLUB organisation while respecting EXOS constraints.
- Place the module alongside existing allocation utilities (under `/utils` or the current allocator directory) and wire it into the build scripts.
- Manage fixed-size slabs of objects backed by `AllocRegion`, expanding or shrinking them through `ResizeRegion` when capacity changes.
- Expose APIs such as `BlockListInit`, `BlockListAllocate`, `BlockListFree`, and maintenance helpers that operate with `LINEAR` addresses and `UINT` sizes.
- Keep the allocator freestanding (no libc), rely on kernel logging macros, and apply SAFE_USE/SAFE_USE_VALID_ID when touching internal pointers.
- Ensure the upcoming radix-tree requests its node allocations through this `BlockList`.
- Add unit tests (`/kernel/source/autotest/`) and DEBUG() statements.

## Stage 2 - Radix Tree Utility (`/utils`)
- Create a new `utils/RadixTree.c` module dedicated to lightweight kernel utilities; add `CMake`/makefile integration if required by existing build scripts.
- Implement a radix-tree tuned for low memory footprint that stores `LINEAR` pointers keyed by `UINT` handles.
- Provide APIs: `RadixTreeCreate`, `RadixTreeDestroy`, `RadixTreeInsert`, `RadixTreeRemove`, `RadixTreeFind`, `RadixTreeIterate` (for diagnostics/cleanup).
- Enforce freestanding constraints (no libc); rely on kernel heap allocators already present in the codebase.
- Keep allocation granular to avoid large contiguous chunks; use BlockList for memory allocation.
- Document threading expectations: the kernel is pre-emptive, so guard the tree with a MUTEX.
- Add unit tests (`/kernel/source/autotest/`) and DEBUG() statements.

## Stage 3 - Handle Mapping Module
- Build on the radix-tree to expose higher-level APIs under `utils/HandleMap.c` (and `.h`):
  - Maintain the global auto-increment counter (`UINT NextHandle`).
  - Functions: `HandleMapInit`, `HandleMapAllocateHandle`, `HandleMapReleaseHandle`, `HandleMapResolveHandle`, `HandleMapAttachPointer`, `HandleMapDetachPointer`.
  - Store pointers as `LINEAR` but convert back to the appropriate typed pointer with helper functions that leverage SAFE_USE/SAFE_USE_VALID_ID macros. Only maintain handle-to-pointer mappings here; reverse mapping is handled elsewhere when required.
  - Add sanity logging with `[HandleMap]` prefixed messages and `%u`/`%p` formatting per logging rules.
  - Ensure error paths return defined `UINT` error codes (no negative numbers).

## Stage 4 - Kernel Integration
- Extend `KERNELDATA` (`kernel/include/Kernel.h`) with a `HANDLE_MAP` descriptor holding:
  - The handle table instance.
  - a MUTEX.
  - The next-handle counter.
- Mirror the structure in architecture-specific `KERNELDATA_I386` and `KERNELDATA_X86_64` if those structs need awareness of global kernel data, otherwise ensure they can reach `Kernel.HandleMap`.
- Initialise the mapping within `kernel/source/KernelData.c` during global data setup, and invoke the init routine during early kernel boot (e.g., `KernelInit` path). Include teardown during shutdown paths if they exist.
- Confirm linkage/section attributes (`SECTION(".data")`) remain intact after adding new members.

## Stage 5 - Syscall Adaptation (`kernel/source/SYSCall.c`)
- Catalogue every syscall entry point and classify parameters/return types that carry kernel pointers today.
- For outbound parameters (kernel → userland):
  - Replace pointer returns with handle allocation before leaving kernel mode.
  - Store the kernel pointer in the global mapping; on failure, return the appropriate error code.
- For inbound parameters (userland → kernel):
  - Resolve the handle to a kernel pointer at the top of the syscall function.
  - Use SAFE_USE/SAFE_USE_VALID_ID macros after resolving to enforce pointer safety.
  - Handle invalid handles by returning existing error codes (ensure consistent behaviour across architectures).
- Update any helper routines invoked by syscalls to accept kernel pointers rather than handles to keep changes localised.
- Review and adjust any ABI structures shared with userland so that fields now carry handles; update comments/documentation accordingly.

## Stage 6 - Documentation & Tooling
- Update `documentation/Kernel.md` to describe the handle mapping subsystem, its invariants, and developer usage guidelines.
- If userland headers expose pointer-based APIs, revise them to advertise handles and explain translation expectations.
- Ensure Doxygen comments on new functions respect the repository template (summary, params, return, 75-character separators).

## Testing Strategy
- Add targeted runtime diagnostics (under DEBUG build) to validate the handle table (e.g., leak detection during shutdown).

## Risk & Follow-Up
- Watch for handle exhaustion; define behaviour when `U32` wraps : ConsolePanic().
- Ensure that pointer lifetimes align with handle lifecycle to avoid dangling entries (consider reference counting if required).
- Confirm no existing kernel components bypass `SYSCall.c` and still leak raw pointers; plan a follow-up audit if needed.
