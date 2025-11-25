# x86-64 Memory Manager Plan

-## Scope and Constraints
- Build `kernel/source/arch/x86-64/x86-64-Memory.c` mirroring `i386-Memory.c` semantics and driver surface (InitializeMemoryManager, temporary mappers, AllocRegion/FreeRegion, fault recovery, PPB handling).
- Paging rules: define a “low 4 KiB window” sized to 1% of total RAM, aligned up to the nearest 2 MiB boundary; only that window uses 4 KiB pages/PTs. Everything above that window uses 2 MiB pages (PS=1) with no PTs. Compute and clamp the window against total RAM.
- Reuse existing 4 KiB page bitmap for the low window; add a dedicated 2 MiB bitmap for the remaining RAM and integrate it into kernel data accessors.
- Keep existing API contracts, types from Base.h, and logging format; follow 4-level paging with recursive slot 510 as defined in `x86-64-Memory.h`.

## Inventory and Design Decisions
- Study `kernel/source/arch/i386/i386-Memory.c` to list all exported symbols, driver wiring, and helper inlines that generic `Memory.c` depends on (MapOnePage/UnmapOnePage, IsValidMemory, ResolveKernelPageFault, AllocPageDirectory/User, MapTemporaryPhysicalPage1..3, AllocRegion/ResizeRegion/FreeRegion, MapIOMemory helpers).
- Confirm low-memory reservations from `KernelStartup` (kernel image, multiboot structs, stack, PPB) and mirror them when computing low 4 KiB usage on x86-64.
- Validate recursive mapping helpers already in `x86-64-Memory.h` (BuildRecursiveAddress, GetCurrentPml4VA/GetPageDirectoryVAFor/GetPageTableVAFor) and decide exact virtual addresses for the temporary pages (TEMP_LINEAR_PAGE_*).

## Paging Layout
- PML4[510] recursive entry; PML4[0] covers low canonical half, PML4[511] for the high kernel half per CONFIG_VMA_KERNEL.
- PDPT/PD setup: below the computed 4 KiB window limit, PD entries point to PTs (size = window, rounded to 2 MiB boundary); above that limit, PD entries are 2 MiB large pages with PS=1 and no PT backing.
- Kernel identity map the low window (1% RAM aligned to 2 MiB) with 4 KiB pages; this region is reserved for MMIO/VGA/BIOS/temp mappings and should not host generic high-memory allocations. Map kernel image at VMA_KERNEL with 2 MiB pages (aligned) except for the low 4 KiB PT reuse when needed for early devices.
- Temporary mapping paths use the low PTs; ensure invlpg path targets the canonical address.

## Bitmap Strategy
- Keep current PPB (4 KiB granularity) to track only the low window (size = low window / 4 KiB). Initialize it from `KernelStartup.PageCount` and the computed window limit.
- Add a 2 MiB bitmap:
  - New storage in kernel data + getters/setters mirroring PPB access.
  - Size derived from total RAM above the low window, rounded to 2 MiB frames.
  - Set bits for reserved ranges: kernel image (aligned to 2 MiB), page tables, PML4/PDPT/PD pools, MMIO reservations, ACPI/SMBIOS low windows (if mapped with 2 MiB), BAR maps that land above the low window.
- Update `UpdateKernelMemoryMetricsFromMultibootMap` and `MarkUsedPhysicalMemory` to fill both bitmaps with multiboot map data (usable vs reserved) honoring the 2 MiB granularity for the high map.

## Allocators and Mapping Functions
- `AllocPhysicalPage`/`FreePhysicalPage`: route to the 4 KiB bitmap when physical < low-window-limit; otherwise operate on the 2 MiB bitmap and return addresses aligned on 2 MiB boundaries. Guard against mixed-size allocations.
- `AllocRegion`/`ResizeRegion`/`FreeRegion`: ranges strictly below the low-window-limit are always 4 KiB; ranges at or above that limit are always 2 MiB (PS=1). Reject any request that would straddle the low-window-limit instead of mixing granularities. Ensure `MEMORY_REGION_DESCRIPTOR` stores `Granularity` (4K vs 2M) and counts pages accordingly.
- `AllocPageDirectory`/`AllocUserPageDirectory`: allocate PML4 + PDPT + PD structures with 2 MiB backing frames, populate kernel space entries by cloning kernel PML4 slots, and pre-create the low PT for the window.
- `MapTemporaryPhysicalPage[1..3]` + `MapIOMemory/UnMapIOMemory`: write PTEs in the low PT, invalidating with `invlpg`; refuse mappings in the high region that are not 2 MiB-aligned unless explicitly split into low PT windows.

## Fault Handling and Validation
- Implement `IsValidMemory` and `ResolveKernelPageFault` using 4-level walks: verify PML4/PDP/PDE presence, handle large PDE (2 MiB) cases, and replicate kernel mappings into user CR3 when faults hit kernel addresses.
- Add assertions/logs mirroring i386 wording (`[FunctionName]` prefix, `%p`/`%x` usage) to trace 2 MiB vs 4 KiB paths.
- Keep SAFE_USE/SAFE_USE_* expectations by ensuring kernel pointers in >2 MiB regions are backed by 2 MiB pages.

## Bootstrapping Steps
- Provide early identity maps for the low window (1% RAM rounded to 2 MiB) with 4 KiB PT, install recursive PML4 entry, load CR3, and enable PAE/long mode paging in the boot path (aligned with `x86-64.c` init sequence).
- Ensure `G_TempLinear1..3` reservation in `.bss` and validated after paging enable.
- Update linker script expectations if kernel sections cross 2 MiB boundaries to keep 2 MiB alignment for PDE large pages.

## Verification
- Build checklist:
  - i386 build still passes (no shared code regressions).
  - x86-64 build boots with new paging, kernel log shows no faults, and temporary mapping APIs work.
  - Memory map dumping matches multiboot map with 2 MiB granularity outside low window.
- Update `documentation/Kernel.md` with the new paging split (4 KiB up to low-window-limit, 2 MiB elsewhere) once implemented.

## Implementation Steps
- [ ] Step 1 - Compute low-window-limit = round_up(1% of total RAM, 2 MiB); clamp against total RAM; publish helper for reuse.
- [ ] Step 2 - Extend kernel data with 2 MiB bitmap storage/getters/setters; size from high memory; update `UpdateKernelMemoryMetricsFromMultibootMap` to fill both bitmaps using the window split.
- [ ] Step 3 - Implement `x86-64-Memory.c` mirroring `i386-Memory.c`: driver entry, temporary mappers, page flag helpers, MapOnePage/UnmapOnePage with 4 KiB PT only in low window; 2 MiB PDEs elsewhere.
- [ ] Step 4 - Wire allocators: `AllocPhysicalPage`/`FreePhysicalPage` pick bitmap by physical address; `AllocRegion`/`ResizeRegion`/`FreeRegion` enforce window split and reject straddling; set `Granularity` in descriptors.
- [ ] Step 5 - Build page directory allocator for user/kernel CR3: allocate PML4/PDPT/PD with 2 MiB backing, pre-create low PTs, install recursive slot, clone kernel slots.
- [ ] Step 6 - Implement `IsValidMemory`/`ResolveKernelPageFault` with 4-level walks handling 2 MiB pages; mirror logging patterns.
- [ ] Step 7 - Update MMIO mapping helpers to stay in the low window or enforce 2 MiB alignment when above; ensure temp mappings use low PT; add required `invlpg` paths.
- [ ] Step 8 - Adjust boot path to set up early low-window identity map, recursive mapping, CR3 load; validate linker alignment for 2 MiB kernel mapping.
- [ ] Step 9 - Test: build x86-64, boot, confirm no faults; validate bitmap accounting, temporary maps, and region allocations across the window boundary; update `documentation/Kernel.md`.
