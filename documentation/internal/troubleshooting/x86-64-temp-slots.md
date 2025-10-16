# x86-64 bootstrap triple fault when switching CR3

## Symptom
As soon as `ArchInitializeMemoryManager` installs the freshly built
page tables, execution dies with a triple fault. The last log lines are
printed by `AllocPageDirectory`:

```
[AllocPageDirectory] PML4 cleared
[AllocPageDirectory] PML4[0]=0000000000000000, PML4[511]=0000000000000000, PML4[253]=0000000000000000, PML4[510]=0000000000000000
[ArchInitializeMemoryManager] New page directory: 0000000000400000
<<TRIPLE FAULT>>
```

The address reported for the new CR3 (`0x400000`) is correct, yet every
PML4 slot still reads as zero. Loading such a hierarchy guarantees an
immediate #PF and, because the IDT that could service that fault also
lives in unmapped memory, the processor escalates directly to a triple
fault.【F:kernel/source/arch/x86-64/x86-64.c†L415-L605】【F:kernel/source/arch/x86-64/x86-64.c†L1000-L1038】

## What actually happens inside `AllocPageDirectory`
The function allocates brand new paging structures for three regions
(low identity map, the high-half kernel, and the user-mode task-runner
stub). Each allocation uses the temporary mapping helpers located at
`X86_64_TEMP_LINEAR_PAGE_{1,2,3}` so the freshly allocated physical
pages can be cleared and populated before they get wired into the
hierarchy.【F:kernel/source/arch/x86-64/x86-64.c†L342-L604】【F:kernel/source/Memory.c†L556-L612】

Every call to `MapTemporaryPhysicalPage1` reuses the same virtual slot
(`0xFFFFFFFF80100000`). The helper updates the *current* page tables via
`MapOnePage`, so the slot always points at “the most recently mapped
page”.【F:kernel/source/Memory.c†L556-L600】 During the setup sequence the
slot is repointed several times:

1. Low-memory PDPT (physical `0x401000`).
2. Kernel PDPT (`0x405000`).
3. Task-runner PDPT (`0x408000`).
4. Finally, the freshly allocated PML4 page (`0x400000`).

The debug build with extra instrumentation confirms each remapping:

```
[MapOnePage] Mapped FFFFFFFF80100000 to 0000000000401000
…
[MapOnePage] Mapped FFFFFFFF80100000 to 0000000000408000
…
[MapOnePage] Mapped FFFFFFFF80100000 to 0000000000400000
```

The final line should mean that when `AllocPageDirectory` writes the
four PML4 entries it is targeting physical page `0x400000`. Instead, the
reads that immediately follow still return zero, which means the writes
never touched that page at all.【F:kernel/source/arch/x86-64/x86-64.c†L526-L583】

## Root cause
The last `MapTemporaryPhysicalPage1` call (the one that should attach
`0x400000` to the temporary slot) silently fails when the new mapping is
loaded. `MapOnePage` first checks the page-directory entry that covers
`0xFFFFFFFF80100000`. Under the bootstrap tables the slot lives in the
kernel half, but only the lower half of that PDE is populated. When the
kernel was linked it also claimed that address for regular code (it is
offset `0x100000` from `VMA_KERNEL`). The loader therefore installs a
*large-page* mapping there (`2 MiB` block starting at physical
`0x300000`). As soon as `MapOnePage` tries to rewrite that PDE so the
slot can host a normal 4 KiB mapping, the guard in the helper notices
that the existing entry is marked as a 2 MiB page (`PAGE_FLAG_PAGE_SIZE`
set) and refuses to override it. No panic is emitted—the helper simply
returns with the old mapping still active.【F:kernel/source/Memory.c†L533-L612】

The kernel then calls `MemorySet` and `WritePageDirectoryEntryValue`
through that stale mapping. All four writes therefore land on whatever
physical page was mapped there previously (the task-runner PDPT), while
the actual PML4 page at `0x400000` remains full of zeroes. The immediate
read-back in the log shows exactly that outcome. When CR3 is loaded the
CPU sees a top-level table with no present entries and faults straight
away.【F:kernel/source/arch/x86-64/x86-64.c†L524-L583】

## Takeaways
* The temporary slots are convenient, but they sit inside the same
  high-half window as normal kernel code. If the bootstrap tables cover
  that area with a 2 MiB large page, `MapOnePage` will refuse to
  downgrade it to a 4 KiB mapping. The failure is silent unless the
  caller checks the return value.
* When the temporary slot stays mapped to an earlier page, every access
  through that window hits the wrong physical frame. In this case it
  left the brand-new PML4 page untouched, so the kernel tried to run with
  an empty hierarchy and triple-faulted immediately.
* Before switching CR3, verify that the temporary slot actually points
  at the pages you are about to populate (or add explicit error handling
  when the downgrade from a large page fails).
