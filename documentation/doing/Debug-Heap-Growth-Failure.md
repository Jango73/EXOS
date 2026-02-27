### Kernel heap growth failure during large network transfer

-   Reproduced in x86-32 network smoke test with `netget` on a 2 MiB
    payload (`smoke-very-large.txt`).
-   Evidence from `log/kernel-x86-32-mbr-debug.log`:
    -   `ERROR > [TryExpandHeap] ResizeRegion failed for heap at 0xc0404000 (from 0x200000 to 0x400000)`
    -   `ERROR > [KernelHeapAlloc] Allocation failed`
    -   `DEBUG > [HTTP_WriteBodyData] Failed to write 1024 bytes (only 0 written)`
-   Impact: large HTTP transfers abort because kernel memory growth
    fails under load.
-   Required resolution:
    -   fix kernel heap expansion reliability under network pressure,
    -   guarantee bounded/fallback behavior when expansion cannot
        proceed,
    -   keep transfer path stable for payloads \>= 2 MiB in smoke tests.

• Credible causes (ranked by probability) for Kernel heap growth failure
during large network transfer:

1.  Very likely: heap expansion requires contiguous virtual space
    immediately after the heap, and that space is already occupied.
    Evidence: the log shows Additional region not free at 0xc0604000,
    then TryExpandHeap failure from 0x200000 to 0x400000 in
    log/kernel-x86-32-mbr-debug.log (/home/jango/code/exos/log/kernel-
    x86-32-mbr-debug.log). Code: ResizeRegion refuses growth if the
    adjacent zone is not free (x86-32-Memory.c
    (/home/jango/code/exos/kernel/source/arch/x86-32/x86-32-Memory.c),
    lines \~1394-1397).

2.  Very likely: the region immediately after the heap is consumed by
    other kernel allocations performed with the same mechanism
    (AllocKernelRegion in first-fit "at or over" mode). Credible
    candidates:

-   task system stacks (x86-32.c
    (/home/jango/code/exos/kernel/source/arch/x86-32/x86-32.c), lines
    \~489-490),
-   memory region descriptor slabs (Memory-Descriptors.c
    (/home/jango/code/exos/kernel/source/Memory-Descriptors.c), lines
    \~71-76),
-   RAMDisk memory (RAMDisk.c
    (/home/jango/code/exos/kernel/source/drivers/storage/RAMDisk.c),
    line \~427). In this design, a single persistent allocation nearby
    blocks any in-place heap growth.

3.  Likely: absence of fallback strategy when ResizeRegion fails (no
    heap relocation, no secondary heap, no degraded mode). TryExpandHeap
    attempts a single in-place growth and then fails immediately (Heap.c
    (/home/jango/code/exos/kernel/source/Heap.c), lines \~210-214).

4.  Likely: initial margin too small in x86-32 (initial kernel heap = 2
    MiB) for this transfer + FS write + network scenario.
    KERNEL_PROCESS_HEAP_SIZE is 2 MiB in x86-32 (Process.h
    (/home/jango/code/exos/kernel/include/process/Process.h), line
    \~124). The netget path writes continuously (fwrite 1024 bytes) to
    the FS; on the kernel side, EXT2 write operations trigger temporary
    kernel heap allocations (indirect buffers, etc.) (EXT2-Storage.c
    (/home/jango/code/exos/kernel/source/drivers/filesystems/EXT2-Storage.c),
    e.g. lines \~308, \~368, \~412).

5.  Possible: heap fragmentation increasing expansion pressure. Free
    does not yet coalesce blocks (TODO) (Heap.c
    (/home/jango/code/exos/kernel/source/Heap.c), lines \~452-454).
    Therefore, even without leaks, the expansion boundary can be reached
    earlier.

Important point: /temp in this scenario is mounted from the active FS
(SourcePath="exos/temp"), not from the ramdisk (exos.toml
(/home/jango/code/exos/build/image/x86-32-mbr-debug-ext2/boot-hd/ext2-
root/exos.toml), lines \~43-45). So the primary cause remains kernel
heap growth and its contiguity constraint, not direct "all-in-heap"
storage of /temp.
