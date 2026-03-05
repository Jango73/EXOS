# File System Scalability Plan

## Goals

- [ ] Guarantee bounded memory usage during folder listing (`dir`, `dir -r`, wildcard paths).
- [ ] Guarantee stable responsiveness on very large folders.
- [ ] Keep `DF_FS_OPENFILE/DF_FS_OPENNEXT` behavior compatible.

## Audit (completed)

### NTFS

- [x] Identified full-folder materialization in wildcard open (`NTFS-VFS.c`).
- [x] Identified full-folder materialization in path component lookup (`NTFS-Path.c`).
- [x] Identified this as the primary scalability blocker.

### EXT2

- [x] Verified incremental enumeration (no full-folder preallocation).
- [x] Identified high per-entry inode metadata cost.

### FAT32 / FAT16 / EXFS

- [x] Verified no full-folder preallocation pattern.
- [x] Identified repeated directory cluster/page rereads in `OpenNext()`.

### PackageFS / SystemFS

- [x] Verified PackageFS cursor-based enumeration.
- [x] Verified SystemFS mostly propagates mounted FS behavior.

### Global VFS lock scope

- [x] Identified `MUTEX_FILESYSTEM` held across potentially long `DF_FS_OPENFILE` calls (`File.c`).

## Execution Checklist

## Phase 1 - NTFS hard fix (current)

- [x] Add bounded NTFS enumeration API with start index + max entries window.
- [x] Replace NTFS wildcard listing from full materialization to chunked streaming.
- [x] Rewrite NTFS path component lookup with early-exit search and no full-folder allocation.
- [ ] Validate no giant allocation burst during NTFS large-folder listing (runtime scenario pending).

## Phase 2 - Global lock containment

- [x] Reduce `MUTEX_FILESYSTEM` hold scope in `File.c` during slow FS open paths.
- [x] Document lock contract in `documentation/Kernel.md`.

## Phase 3 - FAT/EXFS enumeration efficiency

- [ ] Add per-handle directory state cache to avoid unconditional reread in `OpenNext()`.
- [ ] Validate behavior compatibility.

## Phase 4 - EXT2 optimization

- [ ] Optimize heavy metadata reads during large directory enumeration.
- [ ] Validate behavior compatibility.

## Validation Matrix

For each FS (`NTFS`, `EXT2`, `FAT32`, `FAT16`, `EXFS`, `PackageFS`, `SystemFS` wrapper paths):

- [ ] `dir`
- [ ] `dir -r`
- [ ] wildcard listing
- [ ] deep path lookup in large trees
- [ ] very large folder stress (100k+ entries)

Success criteria:

- [ ] No large heap expansion bursts caused by listing alone.
- [ ] Monotonic listing progress.
- [ ] Stable keyboard responsiveness during paging.
- [ ] No regression on `DF_FS_OPENNEXT` semantics.
