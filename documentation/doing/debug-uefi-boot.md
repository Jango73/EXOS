# Factual marker observation

- Platform: Predator (x86-64 UEFI split debug).
- Marker sequence is visible and non-overlapping (no index collision observed).
- Last consistently reported visible markers before hang: `48`, `49`, `50`, then later `50` to `54`.
- Marker `55` was not observed in the same failing path.
- Machine does **not** reboot in this state (no confirmed triple fault on Predator for this case).
- Observed behavior is an infinite loop/hang (fan ramps up, no forward progress).

## Function mapping (search range)

- `48` / `49` / `50` are in `InitializeMemoryManager()` (`kernel/source/arch/x86-64/x86-64-Memory-HighLevel.c`).
- `52` / `53` are in `InitializeRegionDescriptorTracking()` (`kernel/source/Memory-Descriptors.c`).
- `54` is at entry of `GrowDescriptorSlab()` (`kernel/source/Memory-Descriptors.c`), immediately before:
  - `PHYSICAL Physical = AllocPhysicalPage();`
- `55` is immediately after `AllocPhysicalPage()` returns in `GrowDescriptorSlab()`.

## Current narrowed range

- Investigation range is the call path:
  - `InitializeMemoryManager()`
  - `InitializeRegionDescriptorTracking()`
  - `GrowDescriptorSlab()`
  - `AllocPhysicalPage()`
- Practical hotspot: inside `AllocPhysicalPage()` (or below), because `54` is seen and `55` is not.

# Factual observations (Predator)

- Full visible marker set reported by user (before extra probes):
  - `0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 16, 17, 18, 19, 20, 22, 23, 25, 26, 27, 28, 29, 30, 31, 41, 42, 43, 44, 45, 46, 47, 48, 49, 50, 51, 52, 53, 54, 67, 68, 69`
- Additional probes in `AllocPhysicalPage()`:
  - Seen: `71, 72, 73, 74, 76, 77`
  - Not seen: `75`
  - Meaning: `AllocPhysicalPage()` enters, acquires lock path, scans bitmap, finds a page, exits successfully.
- Additional probes at `GrowDescriptorSlab()` call site:
  - Seen: `78`
  - Not seen: `79`, `80`
  - Meaning: execution reaches the `AllocPhysicalPage()` call site in `GrowDescriptorSlab()`, but post-call marker is not observed in this instrumentation pass.
- Additional probes in `LockMutex()`:
  - Seen: `81, 82, 83, 95, 96`
  - Not seen: `84..94`
  - Meaning: path is "no 2 valid tasks" fallback (`Ret = 1`), no wait-loop block.

## Marker integrity checks

- A repository script now validates marker uniqueness and dynamic-range separation:
  - `scripts/utils/check-boot-markers.js`
- The script checks:
  - all kernel `BootStageMarkerFromConsole(...)` marker indexes,
  - all UEFI `BootUefiMarkStage(...)` enum-based stage indexes,
  - dynamic driver-marker ranges from `kernel/source/Kernel.c`.
- Last run result: `OK` (no collisions).
