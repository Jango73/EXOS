# Console Mutex Refactoring Plan

## Scope

This plan tracks the refactoring of console synchronization to remove blocking waits under a shared global mutex and reduce contention across console state and rendering paths.

Goal:
- keep current behavior,
- remove lock amplification around pager and scroll paths,
- keep a safety guard for unexpected long recursive waits.

Out of scope:
- split-console architecture redesign,
- asynchronous debug rendering pipeline redesign.

## Problem Summary

The console stack uses one shared mutex (`MUTEX_CONSOLE`) across:
- console state updates (cursor, paging, regions),
- rendering operations (text/framebuffer/backend),
- operations that can block for input (pager wait).

Nested call chains (`ConsolePrint` -> `ConsolePrintString` -> `ConsolePrintChar` -> `ScrollConsole` -> pager) can increase lock recursion depth and hold the shared lock during blocking waits.

## Target Design

Introduce explicit lock domains:

1. `ConsoleStateMutex` / `MUTEX_CONSOLE_STATE`
- protects mutable console state and region metadata.
- examples: cursor positions, paging counters, region state.

2. `ConsoleRenderMutex` / `MUTEX_CONSOLE_RENDER`
- protects rendering backend interactions and framebuffer rendering critical sections.
- examples: backend draw operations, text cell rendering, cursor visibility rendering.

Ordering rule:
- if both are required: acquire `MUTEX_CONSOLE_STATE` then `MUTEX_CONSOLE_RENDER`.
- never acquire in reverse order.

Hard rule:
- no input wait (`PeekChar`, `GetKeyCode`, sleep loops) while holding any console mutex.

## Migration Steps

### Step 1 - Introduce split mutexes
- [ ] Add `ConsoleStateMutex` and `ConsoleRenderMutex`.
- [ ] Add aliases `MUTEX_CONSOLE_STATE` and `MUTEX_CONSOLE_RENDER`.
- [ ] Keep `MUTEX_CONSOLE` available temporarily for compatibility paths.
- [ ] Add comments documenting lock ownership rules and lock order.

Acceptance:
- build passes x86-32 and x86-64 debug.
- no behavior change in non-split and split modes.

### Step 2 - Pager path first (critical)
- [ ] Refactor pager wait flow to:
  - prepare prompt/state under lock,
  - release lock(s),
  - wait for input with no console lock held,
  - re-acquire state lock and apply paging result.
- [ ] Keep lock hold windows short and explicit.

Acceptance:
- no freeze during `dir -r` paging in split mode under stress.
- keyboard remains responsive while pager is visible.

### Step 3 - Convert core print/scroll path
- [ ] Convert `ConsolePrint`, `ConsolePrintString`, `ConsolePrintChar`, `ScrollConsole`, `SetConsoleCursorPosition` to new lock domains.
- [ ] Remove nested lock amplification from the main print path.
- [ ] Keep lock ordering checks in code comments and review notes.

Acceptance:
- no regression in console output correctness.
- no deadlock in recursive print scenarios.

### Step 4 - Convert remaining console helpers
- [ ] Region clear/scroll helpers.
- [ ] Framebuffer cursor helpers.
- [ ] Debug region output path.

Acceptance:
- split and non-split rendering correctness unchanged.
- no lock inversion found in review.

### Step 5 - Compatibility cleanup
- [ ] Remove temporary uses of `MUTEX_CONSOLE` in converted paths.
- [ ] Keep only required compatibility wrappers, or remove `MUTEX_CONSOLE` entirely if no longer needed.
- [ ] Update kernel documentation for final lock model.

Acceptance:
- no reference to legacy shared console lock in converted modules.
- documentation updated.

## Safety Guard Policy

Keep the mutex reentrant wait guard in `LockMutex`:
- one-time `ERROR` after 2 seconds when waiting on a recursively held mutex,
- force unlock after 5 seconds as emergency recovery.

This guard is a safety net, not a substitute for correct lock scoping.

## Validation Matrix

For each completed step:
- [ ] Build x86-32 debug.
- [ ] Build x86-64 debug.
- [ ] Boot smoke test x86-32.
- [ ] Boot smoke test x86-64.
- [ ] Manual pager test: `dir -r` (non-split).
- [ ] Manual pager test: `dir -r` (split).
- [ ] Manual keyboard responsiveness test while pager prompt is displayed.

## Risk Notes

- Force unlock can preserve availability but can break invariants in owner critical sections.
- During migration, mixed old/new locking paths can introduce temporary lock-order risks.
- Strict review is required for every function that both reads state and calls render/backend routines.

## Tracking Log

### Status
- Owner: kernel
- Branch: `dev/next`
- Global status: `in progress`

### Step Progress
- Step 1: `pending`
- Step 2: `pending`
- Step 3: `pending`
- Step 4: `pending`
- Step 5: `pending`

### Notes
- Use this section to add dated implementation notes and decisions.
