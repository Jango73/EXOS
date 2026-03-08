# Windowing Mutex Granularity Plan

## Objective

Reduce desktop and windowing lock contention while removing deadlock-prone lock order inversions.

The design must ensure:

- No global desktop lock held across message callbacks.
- No message post performed while a structural desktop or window lock is held.
- A single, enforced lock acquisition order.

## Root Problem Summary

The desktop path combines several unrelated concerns under broad lock scopes:

- Message routing and delivery.
- Desktop tree traversal and z-order mutations.
- Window state updates.
- Timer scanning and timer-driven message posting.

When these concerns overlap in one critical section, reentrant paths such as drag, redraw, and timer delivery produce lock dependency cycles.

## Target Lock Model

### Lock Domains

Split lock ownership by resource domain:

- `TaskMessageMutex`:
  protects task and process message queue bookkeeping.
- `DesktopTreeMutex`:
  protects parent/children links and z-order list structure.
- `DesktopStateMutex`:
  protects desktop interaction state (capture, focus, focused process, drag offsets).
- `DesktopTimerMutex`:
  protects timer list structure and next-tick updates.
- `WindowMutex` (per window):
  protects a window's local state (rectangles, dirty region, status, style flags).

### Lock Order Contract

All code paths must acquire locks only in this order:

1. `TaskMessageMutex`
2. `DesktopTreeMutex`
3. `DesktopStateMutex`
4. `WindowMutex`
5. `GraphicsContextMutex`

A path that needs a lock earlier in the order must release later locks first.

## Mandatory Behavioral Rules

1. Never call `Window->Function` while holding `TaskMessageMutex`, `DesktopTreeMutex`, or `DesktopStateMutex`.
2. Never call `PostMessage` while holding `DesktopTreeMutex`, `DesktopStateMutex`, or any `WindowMutex`.
3. Never iterate mutable child lists recursively while callbacks can mutate z-order or tree structure.
4. Any tree traversal used for delivery or invalidation must use a stable snapshot.
5. Timer code must post messages only after releasing `DesktopTimerMutex`.

## Structural Refactor Plan

### Step 1: Lock API and Documentation Baseline

- [x] Introduce explicit lock-role naming in desktop code (`Tree`, `State`, `Timer`).
- [x] Document lock order contract in desktop headers and implementation comments.
- [x] Add debug-only lock order assertions (`AcquireLockRole`, `ReleaseLockRole`) to fail fast on inversion.

### Step 2: Safe Message Dispatch Path

- [x] Convert dispatch to direct-target routing:
  - Resolve and validate target window under tree lock.
  - Release tree lock.
  - Lock only target `WindowMutex`.
  - Invoke callback.
- [x] Remove recursive dispatch traversal over mutable child lists.

### Step 3: Safe Post Path

- [ ] Keep post routing independent from deep tree traversal where possible.
- [ ] Resolve owner task/window identity under minimal lock scope.
- [ ] Release structural locks before queue operations when lock order requires it.
- [ ] Keep queue coalescing local to queue locks only.

### Step 4: Drag and Z-Order Mutations

For `BringWindowToFront` and move paths:

- [ ] Phase A (under `DesktopTreeMutex` + local window locks as needed):
  - mutate z-order,
  - compute impacted rectangles,
  - snapshot targets to notify.
- [ ] Phase B (outside structural locks):
  - enqueue invalidations/draw requests.

### Step 5: Broadcast and Tree Walks

- [ ] Replace recursive in-lock broadcast with two-phase snapshot traversal:
  - collect recipients under tree lock,
  - unlock,
  - post/request draw for each recipient.

### Step 6: Timer Delivery Isolation

- [ ] Under `DesktopTimerMutex`:
  - collect due timers,
  - update next due ticks.
- [ ] Outside lock:
  - post `EWM_TIMER` messages.

## Data Ownership Notes

- `WindowMutex` owns geometry and dirty regions for one window only.
- Desktop-level locks must not be used as substitutes for per-window state safety.
- Tree lock owns link integrity, not drawing state.

## Validation Strategy

### Runtime Assertions

Enable debug checks for:

- lock order inversions,
- posting under forbidden lock roles,
- callback execution under forbidden global desktop locks.

### Stress Scenarios

Run dedicated tests for:

1. Dragging a timed window repeatedly across overlapping windows.
2. Rapid z-order changes while periodic timer redraw is active.
3. Cursor movement and redraw overlap during drag.
4. Repeated open/close of timed and non-timed windows.

### Pass Criteria

- No deadlock under long drag stress.
- No unbounded lock hold logs.
- No corruption in z-order traversal.
- No missed timer redraw delivery for active windows.

## Review Checklist (PR Gate)

- Does this change introduce a new lock acquisition order?
- Does any path call `PostMessage` while a structural lock is held?
- Does any path call a window callback under desktop global locks?
- Does any mutable tree traversal happen without a snapshot?
- Are timer posts performed outside timer lock scope?

## Migration Priority

Implementation order should be:

1. Dispatch path.
2. Broadcast path.
3. Bring-to-front and drag mutation path.
4. Timer path.
5. Remaining redraw and cursor helper paths.

This order reduces the highest-impact deadlock surfaces first while preserving behavior incrementally.
