# What

- Boot on Predator using x86-64 uefi debug split build

# Factual marker observation

- Platform: Predator (x86-64 UEFI split debug).
- Marker sequence is visible and non-overlapping (no index collision observed).
- Observed behavior is a hang (no reboot, no heavy work : fan silent).

# Factual observations

- Full visible marker set reported by user:
  - `0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 16, 17, 18, 19, 20, 22, 23, 25, 26`

## Marker integrity checks

- A repository script validates marker uniqueness and dynamic-range separation:
  - `scripts/utils/check-boot-markers.js`
- The script checks:
  - all kernel `BootStageMarkerFromConsole(...)` marker indexes,
  - all UEFI `BootUefiMarkStage(...)` enum-based stage indexes,
  - dynamic driver-marker ranges from `kernel/source/Kernel.c`.
- Last run result: `OK` (no collisions).
