# Desktop State Refactor Plan

## Problem Statement
- Desktop state was tied to one always-live static singleton.
- The static instance did not go through the same initialization path as dynamically created desktops.
- This caused one functional divergence: the shell-visible desktop did not receive the `ShellBar` created in `CreateDesktop()`.
- Several subsystems used that singleton as an implicit fallback instead of modeling the absence of a desktop explicitly.

## Goals
- Remove architectural dependence on a statically prebuilt desktop singleton.
- Allow the system to operate correctly when no desktop is focused.
- Keep input routing valid when no desktop exists by falling back to the focused process, then `KernelProcess`.
- Move theme runtime state out of `DESKTOP` and into kernel-global state.
- Ensure any desktop used as the shell-visible desktop instance is initialized through the same path as other desktops, or through one shared initializer with identical behavior.

## Non-goals
- No ad-hoc compatibility fallback to a half-initialized static desktop.
- No silent recreation of focus through a singleton desktop when focus is invalid.
- No duplicate initialization path for the static desktop and dynamic desktops.

## Target Invariants
- A desktop may legitimately be absent.
- `GetActiveDesktop()` may return `NULL`.
- `GetFocusedProcess()` must still return a valid process whenever possible.
- Input routing policy:
  - active desktop first when present,
  - otherwise focused process,
  - otherwise `KernelProcess`,
  - otherwise drop the event.
- Theme runtime state must not depend on any desktop object existing.
- The shell-visible desktop, when present, must be a normally initialized desktop instance, not a special-case partially initialized object.

## Work Items

### Step 1 - Theme runtime decoupling
- [x] Move theme runtime ownership out of `DESKTOP`.
- [x] Store active/staged/built-in runtime state in `KernelData` or another kernel-global state holder.
- [x] Update theme runtime resolution so desktop lookup is optional, not required.
- [x] Remove the static desktop singleton as the theme fallback object.

### Step 2 - Focus model cleanup
- [x] Replace `Kernel.FocusedDesktop` with nullable `Kernel.ActiveDesktop`.
- [x] Remove singleton fallback from desktop activation state.
- [x] Add explicit global `Kernel.FocusedProcess`.
- [x] Ensure process death cleans up focus without resurrecting a desktop singleton.

### Step 3 - Input routing cleanup
- [ ] Update keyboard input routing to work without an active desktop.
- [x] Update mouse input routing so screen-rect and cursor operations are conditional on desktop presence.
- [ ] Preserve "drop when unroutable" semantics explicitly instead of relying on singleton fallback state.

### Step 4 - Static desktop lifecycle cleanup
- [x] Replace direct static singleton assumptions with explicit publication/access helpers.
- [x] Remove the static desktop storage entirely.
- [x] Ensure the shell desktop receives the same root window, dock host, shellbar, timer list, and dispatcher setup as any other desktop.

### Step 5 - Direct callsite cleanup
- [x] Audit direct singleton desktop uses in:
  - process bootstrap,
  - focus bootstrap,
  - display session,
  - shell commands,
  - theme runtime.
- [x] Replace raw singleton access with explicit desktop creation/reuse logic.

### Step 6 - Validation
- [x] Build x86-32 debug.
- [ ] Validate `desktop show` creates a shellbar on the shell desktop.
- [ ] Validate theme operations still work before and after desktop activation.
- [ ] Validate keyboard and mouse input when no desktop is active.
- [ ] Validate no new lock-order or NULL-routing faults appear in logs.

## Open Decisions
- [ ] Whether the shell-visible desktop should exist from early boot or only when desktop mode is first requested.
- [x] Remove duplicated desktop ownership from `DisplaySession`; keep one source of truth in `KernelData`.
- [x] Keep one explicit global `FocusedProcess` field in `KernelData`.
