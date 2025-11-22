# Profiling integration plan (i386 + x86-64)

## Objective
- Add a lightweight, optional kernel profiling system to measure hotspots (e.g., console output) without impacting non-profile builds.

## Ground rules
- Kernel only uses Base.h types/macros and in-tree helpers (no stdlib/stdio).
- Logging strings start with the function name; sizes logged with "%u", other values with "%x", pointers with "%p".
- Keep profiling behind a build-time flag (e.g., CONFIG_PROFILE) so non-profile builds pay zero cost.

## Step-by-step plan
1. [x] **Define config switch**
   - Add a global config toggle (CONFIG_PROFILE) in an existing config header used by both arches.
   - Default OFF; enable with `make PROFILE=1` so profiling code (and shell command) compiles in.

2. [x] **Add profiling API (shared)**
   - Create `kernel/source/Profile.h`/`Profile.c`.
   - API: `ProfileStart(LPPROFILE_SCOPE Scope, const char* Name)`, `ProfileStop(LPPROFILE_SCOPE Scope)`, `PROFILE_SCOPED(name_literal)` macro, `ProfileDump(void)` for reporting.
   - Provide no-op inline stubs when CONFIG_PROFILE is not defined.
   - Use Base.h types: ticks as UINT or PHYSICAL where appropriate; names stored as const char* literals.
   - Doxygen headers + 75-char separators between functions.

3. [x] **Time source**
   - Use current system clock ticks (10 ms resolution via `GetSystemTime()`); keep raw tick values and convert on dump.

4. [x] **Data storage**
   - Use existing `utils/CircularBuffer` with static storage for samples; fixed-size buffer holds `{Name, DurationTicks}` events.
   - Aggregate on dump into a small stats array (count/last/total/max); static, no dynamic allocation.
   - Buffer is initialized on first use; warning on overflow.

5. [x] **Integration points**
   - Wrapped console paths with `PROFILE_SCOPED(...)` (`ConsolePrintChar`, `ScrollConsole`, `ConsolePrintString`, `SetConsoleCharacter`, `SetConsoleCursorPosition`) to sample display latency.
   - Scheduler/drivers can be added later if needed; SAFE_USE* usage unchanged.

6. [x] **Shell command for dump**
   - Added `prof` command in `kernel/source/Shell.c` (always available); invokes `ProfileDump()` and logs stats.
   - If profiling is disabled or there are no samples, `ProfileDump()` emits nothing; no dynamic allocation or sorting beyond current aggregation order.

7. [ ] **Architecture build wiring**
   - Update both script sets to expose a profiling build target:
     - i386: add/clone a script (e.g., `./scripts/i386/4-7-build-profile.sh`) enabling CONFIG_PROFILE.
     - x86-64: mirror the script (e.g., `./scripts/x86-64/4-7-build-profile.sh`).
   - Ensure the flag propagates to the kernel compile definitions for both arches.

8. [ ] **Documentation**
   - Update `documentation/Kernel.md` to describe the profiling facility, API, and usage constraints.
   - Add a short usage note in `documentation/Project.md` or relevant section if needed.

9. [ ] **Validation loop**
   - Build in profiling mode for x86-64, boot QEMU, perform console-heavy operations, run `prof`.
   - Compare Max/Avg ticks for console vs. other subsystems; repeat on i386 to see arch differences.
   - Keep test runs under 15 seconds to avoid oversized logs.

10. [ ] **Tuning and rollout**
    - Adjust buffer size if overflow is frequent; consider per-CPU buckets later if contention observed.
    - If console shows as hotspot, investigate path (e.g., memory moves on scroll) with more granular scopes.
