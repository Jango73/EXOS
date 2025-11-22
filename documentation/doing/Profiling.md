# Profiling integration plan (i386 + x86-64)

## Objective
- Add a lightweight, optional kernel profiling system to measure hotspots (e.g., console output) without impacting non-profile builds.

## Ground rules
- Kernel only uses Base.h types/macros and in-tree helpers (no stdlib/stdio).
- Logging strings start with the function name; sizes logged with "%u", other values with "%x", pointers with "%p".
- Keep profiling behind a build-time flag (e.g., CONFIG_PROFILE) so non-profile builds pay zero cost.

## Step-by-step plan
1. [ ] **Define config switch**
   - Add a global config toggle (CONFIG_PROFILE) in an existing config header used by both arches.
   - Default OFF; when ON, profiling code compiles and shell command is registered.

2. [ ] **Add profiling API (shared)**
   - Create `kernel/source/Profile.h`/`Profile.c`.
   - API: `ProfileStart(const char* Name)`, `ProfileStop(const char* Name)`, `ProfileScoped(name_literal)` macro, `ProfileDump(void)` for reporting.
   - Provide no-op inline stubs when CONFIG_PROFILE is not defined.
   - Use Base.h types: ticks as UINT or PHYSICAL where appropriate; names stored as const char* literals.
   - Doxygen headers + 75-char separators between functions.

3. [ ] **Time source**
   - Use existing cycle counter accessor (e.g., ReadTSC or equivalent) shared across i386/x86-64.
   - Keep raw tick values; convert to ns/ms only in dump to reduce overhead.

4. [ ] **Data storage**
   - Implement a fixed-size circular table (e.g., 4096 entries) of structs containing: Name, Count, LastTicks, TotalTicks, MaxTicks.
   - No dynamic allocation; static storage in Profile.c.
   - Use simple locking strategy that is valid in interrupt and task contexts (e.g., Disable/EnableInterrupts around updates or a lightweight spinlock already present).

5. [ ] **Integration points**
   - Wrap suspected slow paths with `ProfileScoped(...)`:
     - Console path: putc/scroll, buffer flush, full string write.
     - Scheduler tick handler (to measure per-tick overhead).
     - Disk/network drivers on hot paths only if overhead acceptable.
   - Ensure SAFE_USE* macros stay respected when touching kernel pointers.

6. [ ] **Shell command for dump**
   - In `kernel/source/Shell.c`, add a `prof` command compiled only when CONFIG_PROFILE is ON.
   - Command calls `ProfileDump()`, printing per-entry stats: Name, Count, Avg (ns), Max (ns), Total (ms).
   - Sorting: either insertion order or simple selection sort in a static scratch buffer (no heap).
   - Use DEBUG()/VERBOSE() as appropriate; keep output compact.

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
