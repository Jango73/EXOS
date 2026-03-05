# File System Scalability Plan

## Scope

This plan targets scalability failures during folder listing (`dir`, `dir -r`, wildcard paths) across all file systems:

- `NTFS`
- `EXT2`
- `FAT32`
- `FAT16`
- `EXFS`
- `PackageFS`
- `SystemFS` (wrapper)

The objective is to guarantee bounded memory usage and stable latency on very large folders.

## Audit Summary

### NTFS (critical)

Files:

- `kernel/source/drivers/filesystems/NTFS-VFS.c`
- `kernel/source/drivers/filesystems/NTFS-Path.c`
- `kernel/source/drivers/filesystems/NTFS-Index.c`

Findings:

1. Wildcard open path performs full-folder materialization:
   - `NtfsOpenFile()` runs a count pass + full list pass.
   - Allocates `TotalEntries * sizeof(NTFS_FOLDER_ENTRY_INFO)`.
   - For large folders this creates large heap pressure and expansion bursts.
2. Path component lookup repeats the same anti-pattern:
   - `NtfsLookupChildByName()` also does count pass + full list allocation.
   - This affects any deep path resolution under large folders.

Impact:

- Unbounded allocation spikes.
- Long uninterruptible driver work segments.
- Severe latency and poor behavior in very large folders.

### EXT2 (medium)

Files:

- `kernel/source/drivers/filesystems/EXT2-FileOps.c`
- `kernel/source/drivers/filesystems/EXT2-Base.c`

Findings:

1. Enumeration is incremental (good): no full-folder preallocation.
2. One inode read per matched entry in `LoadNextDirectoryEntry()`:
   - expected, but expensive on huge folders.
3. `OpenFile()` holds `FilesMutex` while resolving/wildcard setup.

Impact:

- CPU/IO cost scales linearly with entries.
- No catastrophic heap behavior found.

### FAT32 / FAT16 / EXFS (medium)

Files:

- `kernel/source/drivers/filesystems/FAT32-FileOps.c`
- `kernel/source/drivers/filesystems/FAT16.c`
- `kernel/source/drivers/filesystems/EXFS.c`

Findings:

1. No full-folder preallocation pattern found.
2. `OpenNext()` re-reads the current directory cluster/page at each step:
   - redundant IO and extra latency.
   - still bounded memory.

Impact:

- Scales functionally but with avoidable overhead on huge folders.

### PackageFS (low)

Files:

- `kernel/source/package/PackageFS-File.c`

Findings:

1. Enumeration is cursor-based in memory.
2. No large dynamic list allocation during wildcard listing.

Impact:

- No major scalability risk in listing path.

### SystemFS wrapper (medium by propagation)

Files:

- `kernel/source/SystemFS.c`

Findings:

1. Delegates wildcard/listing to mounted FS.
2. Inherits mounted FS behavior (especially NTFS path).

Impact:

- Wrapper itself is not the memory hotspot.
- Visible behavior is dominated by mounted driver implementation.

### Cross-FS global lock scope (high)

File:

- `kernel/source/File.c`

Finding:

1. `OpenFile()` holds `MUTEX_FILESYSTEM` while invoking FS `DF_FS_OPENFILE`.
2. Slow wildcard/resolve paths in any driver extend global lock hold time.

Impact:

- Cross-subsystem contention and responsiveness degradation.
- Converts one slow FS call into a global bottleneck.

## Correction Strategy

## Phase 1 - NTFS hard fix (mandatory first)

1. Replace full-materialization wildcard flow in `NtfsOpenFile()` with chunked streaming.
2. Add bounded enumeration API in `NTFS-Index`:
   - introduce `StartIndex` + `MaxEntries` semantics.
   - return partial windows without allocating full folder arrays.
3. Rewrite `NtfsLookupChildByName()` to early-exit search:
   - stop at first name match.
   - no count pass, no full array allocation.
4. Keep compatibility with current VFS `DF_FS_OPENNEXT`.

Expected result:

- No giant `KernelHeapAlloc` based on total folder entry count.
- Stable heap behavior on huge folders.

## Phase 2 - Cross-FS lock containment

1. Narrow `MUTEX_FILESYSTEM` hold window in `File.c`:
   - keep list/selection consistency.
   - avoid holding global FS mutex during long driver scans when possible.
2. Define lock contract clearly in code comments and `Kernel.md`.

Expected result:

- One slow FS no longer blocks unrelated opens globally.

## Phase 3 - FAT/EXFS enumeration efficiency

1. Add per-handle directory data cache state to avoid unconditional cluster/page reread in `OpenNext()`.
2. Preserve current behavior and ABI.

Expected result:

- Lower IO overhead for large listings.
- Better latency without memory tradeoffs.

## Phase 4 - EXT2 optimization pass

1. Evaluate inode metadata read strategy for directory listing.
2. Add optional lightweight entry path when metadata is not mandatory.

Expected result:

- Better throughput on very large EXT2 folders.

## Implementation Order

1. `NTFS` streaming wildcard + path lookup early-exit.
2. Global `File.c` lock-scope refactor.
3. `FAT32/FAT16/EXFS` `OpenNext()` cache optimization.
4. `EXT2` metadata-read optimization.
5. Documentation update in `documentation/Kernel.md`.

## Validation Matrix

For each FS (`NTFS`, `EXT2`, `FAT32`, `FAT16`, `EXFS`, `PackageFS`, `SystemFS` wrapper paths):

1. `dir`
2. `dir -r`
3. wildcard open (`*`, `?.*` when supported)
4. deep path lookup in large folder trees
5. 100k+ entries stress scenario

Metrics to validate:

1. No large heap expansion bursts caused by listing alone.
2. Listing progress remains monotonic.
3. Keyboard responsiveness remains stable during paging.
4. No functional regressions in `DF_FS_OPENNEXT` behavior.

## Notes

The current blocker is `NTFS` wildcard/path enumeration strategy.  
Other file systems show bounded-memory behavior but still have optimization debt for large folder throughput.
