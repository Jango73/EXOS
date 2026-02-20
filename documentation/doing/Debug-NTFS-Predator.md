# NTFS/NVMe Predator - Debug Journal and Research Status

Date: 2026-02-12
Context: bare-metal debug (Predator), NTFS access issue on NVMe through `/fs/hd0p2`.

## Objective
- Fix `dir`/`cd` failures on the NTFS NVMe volume.
- Reduce log flooding without hiding useful signals.
- Strictly isolate boot logs (`T0`) from shell command runtime logs (`Txxxxx` after boot).

## Observed symptoms (field)
- `dir /fs/hd0p2` often returns: `Unable to read on volume hd0p2, reason : file system driver refused open/list`.
- Recurring flood: `[NtfsLoadFileRecordBuffer] Invalid file record magic=...`.
- In recent logs, `dir` command errors are at `T19xxx`/`T138xxx` (runtime), distinct from boot `T0`.

## Changes made

### 1) Early-boot timeout infrastructure
Files:
- `kernel/include/Clock.h`
- `kernel/source/Clock.c`

Change:
- Added `HasOperationTimedOut(StartTime, LoopCount, LoopLimit, TimeoutMilliseconds)`.
- Goal: avoid relying only on `GetSystemTime` before `EnableInterrupts`.

### 2) Agent rule documentation
Files:
- `AGENTS.md`

Changes:
- Explicit reminder: `GetSystemTime` does not advance in early boot before `EnableInterrupts`.
- Explicit reference to `KernelLogTagFilter`.
- Added an "Architecture and Reuse Rules (MANDATORY)" section requiring reusable modules instead of local hacks.

### 3) Generic rate-limit module
Files:
- `kernel/include/utils/RateLimiter.h` (new)
- `kernel/source/utils/RateLimiter.c` (new)
- `documentation/Kernel.md`

Change:
- Created a generic `RateLimiter` module (immediate budget + cooldown + suppressed counter).

### 4) NTFS `Invalid file record magic` flood reduction
File:
- `kernel/source/drivers/NTFS-Record.c`

Change:
- Warning is now limited (initial budget then periodic), with `suppressed=%u`.

### 5) Hardening of NTFS parsing/indexing
Files:
- `kernel/source/drivers/NTFS-Base.c`
- `kernel/source/drivers/NTFS-Private.h`
- `kernel/source/drivers/NTFS-Record.c`
- `kernel/source/drivers/NTFS-Index.c`
- `kernel/source/drivers/NTFS-Path.c`

Changes:
- Added `NtfsIsValidFileRecordIndex()` (MFT index geometric validation).
- Added index validation before `NtfsLoadFileRecordBuffer`.
- Strengthened NTFS file-reference decoding.
- Hardened index traversal (less permissive on inconsistent metadata).
- Added targeted instrumentation for path resolution and enumeration failures.
- Added diagnostic counters in `NTFS_FOLDER_ENUM_CONTEXT` (`ref_invalid`, `idx_invalid`, `record_read_fail`, `seq_mismatch`).

### 6) Log filter focused on ongoing debug
File:
- `kernel/source/Log.c`

Changes:
- Increased `KERNEL_LOG_TAG_FILTER_MAX_LENGTH` to 512.
- Enriched filter with relevant NVMe/NTFS tags (`NtfsEnumerateFolderByIndex`, `NtfsReadFileRecord`, `NtfsResolvePathToIndex`, `NtfsLookupChildByName`, etc.).
- Added runtime `OpenFile`, `ResolvePath` tags to trace shell command path handling.

### 7) Runtime SystemFS instrumentation
File:
- `kernel/source/SystemFS.c`

Changes:
- Added explicit logs in `OpenFile`:
  - resolution context (`path`, `wildcard`, `node`, `mount_path`, `remaining`),
  - delegated open failures (direct/wildcard/mounted).

### 8) NTFS VFS wildcard enumeration fix
File:
- `kernel/source/drivers/NTFS-VFS.c`

Change:
- `NtfsLoadCurrentEnumerationEntry()` no longer breaks on the first unreadable entry.
- It skips dead entries and continues searching for a readable one.

### 9) NVMe work already integrated in current status
Files:
- `kernel/include/drivers/NVMe-Core.h`
- `kernel/include/drivers/NVMe-Internal.h`
- `kernel/source/drivers/NVMe-Admin.c`
- `kernel/source/drivers/NVMe-Core.c`
- `kernel/source/drivers/NVMe-Disk.c`
- `kernel/source/drivers/NVMe-IO.c`

Main points:
- Polling-only mode.
- Hybrid timeout (clock + early-boot loop fallback).
- Synchronization/mutex around submit/wait.
- More robust CQ read (`volatile` + explicit copy).
- Namespace-based logical sector size detection (end of hardcoded 512 for I/O).
- Reduced NVMe warning flood (cooldown).

## Tests run
Command relaunched many times:
- `./scripts/build --arch x86-64 --fs ext2 --debug`

Result:
- Build OK after each series of changes listed above.

Note:
- No automated bare-metal test is possible here; final validation depends on Predator feedback.

## What the latest Predator logs show
- Failed `dir` command lines are indeed runtime (`high Txxxxx`), not `T0`.
- We see:
  - `[OpenFile] path=/fs/hd0p2 wildcard=1 ...`
  - then `Mounted wildcard open failed local=*`
  - same for `/fs/hd0p2/Intel`.
- Intermediate conclusion:
  - SystemFS resolution to the mounted volume works,
  - failure happens in wildcard open delegated to the mounted NTFS driver.

## Processed hypotheses and status
- "It is only a log flood issue": FALSE.
- "The issue is only boot/UEFI": FALSE for runtime `dir` failure.
- "The first invalid entry breaks the whole enumeration": likely, fix applied, but user feedback indicates no visible improvement.

## Current status (important)
- Issue is still reproducible on Predator based on latest feedback: "no difference".
- Diagnostic quality is better (we know where it breaks), but the root bug is not neutralized yet.

## Next fix direction (not done)
- Focus `NtfsOpenFile` wildcard path:
  - instrument and validate `TotalEntries`, `StoredEntries`, `MatchCount` (before handle creation),
  - verify actual `Entries[]` content after pattern filtering,
  - trace `NtfsLoadCurrentEnumerationEntry` return value with current index,
  - compare wildcard behavior (`*`) versus direct open (`name` without wildcard).
- If needed: fallback in `NtfsOpenFile` for non-wildcard folder listing through robust enumeration handle when wildcard fails on a partially corrupted volume.

## Current git status
Modified/uncommitted files:
- `AGENTS.md`
- `documentation/Kernel.md`
- `kernel/include/Clock.h`
- `kernel/include/drivers/NVMe-Core.h`
- `kernel/include/drivers/NVMe-Internal.h`
- `kernel/include/utils/RateLimiter.h` (new)
- `kernel/source/Clock.c`
- `kernel/source/Log.c`
- `kernel/source/SystemFS.c`
- `kernel/source/drivers/NTFS-Base.c`
- `kernel/source/drivers/NTFS-Index.c`
- `kernel/source/drivers/NTFS-Path.c`
- `kernel/source/drivers/NTFS-Private.h`
- `kernel/source/drivers/NTFS-Record.c`
- `kernel/source/drivers/NTFS-VFS.c`
- `kernel/source/drivers/NVMe-Admin.c`
- `kernel/source/drivers/NVMe-Core.c`
- `kernel/source/drivers/NVMe-Disk.c`
- `kernel/source/drivers/NVMe-IO.c`
- `kernel/source/utils/RateLimiter.c` (new)

## Commit
- No commit made in this step (explicit user request: do not commit without direct instruction).
