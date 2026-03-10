# Desktop Lock Ownership Audit

## Scope

This audit lists the places in `kernel/source/desktop` that violate the
target rule:

- one lock must be taken only by the owner of the protected data
- that lock must only cover short get/set operations
- callers must use owner-side getters, setters, or snapshot helpers

The immediate symptom visible in
`log/kernel-x86-64-uefi-debug.log` is a lock battle after the first
desktop draw:

- `DeferredWork` waits on a mutex owned by `DesktopDispatcher`
- `DesktopDispatcher` waits on a mutex owned by `DeferredWork`
- the log also reports repeated lock order inversions for `Desktop`

This document lists the places to fix before changing behavior.

## Priority 1: Nested desktop/window locking and cross-owner locking

### `kernel/source/desktop/Desktop-Main.c`

- `GetDesktopScreenRect` at lines 546-576
  - Takes `Desktop->Mutex`, then takes `Desktop->Window->Mutex`.
  - This is a direct desktop-to-window nested lock.
  - Required cleanup:
    - desktop-side getter for root window handle or root screen rect
    - window-side getter for screen rect
    - no caller should lock both objects manually

- `DeleteWindow` at lines 608-667
  - Takes `Desktop->Mutex` to mutate `Desktop->Capture` and
    `Desktop->Focus`.
  - Then takes `This->Mutex`.
  - Then takes `This->ParentWindow->Mutex`.
  - This function crosses three ownership domains directly.
  - Required cleanup:
    - desktop-side setters for capture/focus release
    - window-side child snapshot helper for recursive delete
    - window-side detach helper for parent-child unlink

- `CreateWindow` at lines 807-822
  - Takes `This->ParentWindow->Mutex` and performs multiple operations:
    compute screen rect, reset dirty region, insert child, walk parent
    chain for level.
  - This is a long compound operation under a foreign window lock.
  - Required cleanup:
    - parent-side attach helper
    - parent-side screen-space conversion helper
    - parent-side level/root ancestry snapshot helper

- `FindWindow` at lines 678-703
  - Recurses into children while keeping the current window mutex held.
  - This creates parent-then-child lock chaining for a deep traversal.
  - Required cleanup:
    - child snapshot getter
    - recursive traversal without holding a window lock across descent

- `BringWindowToFront` at lines 1245-1295
  - Takes `Parent->Mutex`, mutates every child order, sorts the list,
    then snapshots children while still holding the lock.
  - This is owner-side, but not a short get/set anymore.
  - Required cleanup:
    - parent-side reorder helper
    - child snapshot returned by owner after reorder

### `kernel/source/desktop/Desktop-Graphics.c`

- `DefaultSetWindowRect` at lines 350-399
  - Takes `Window->Mutex`, reads `ParentWindow`, then calls
    `ClampWindowRectToParentWorkRect`.
  - `ClampWindowRectToParentWorkRect` at lines 285-340 takes
    `Parent->Mutex` and reads parent rect/work rect/status.
  - The move path therefore mixes child-owned and parent-owned data
    with manual locking.
  - Required cleanup:
    - window-side getter for parent handle
    - parent-side getter for effective work rect
    - window-side setter for rect/screen rect update

- `InvalidateSiblingWindowsOnUncoveredRect` at lines 185-244
  - Takes `Parent->Mutex`, snapshots children manually, then takes every
    sibling window mutex one by one.
  - This is a cross-owner traversal orchestrated by a third function.
  - Required cleanup:
    - parent-side child snapshot API
    - window-side snapshot for `Status`, `Order`, `ScreenRect`

- `BuildWindowDrawClipRegion` at lines 453-529
  - Takes `This->Mutex`, consumes dirty region, then takes
    `ParentWindow->Mutex` and traverses siblings.
  - This is one of the central draw-path ownership violations.
  - Required cleanup:
    - window-side dirty-region consume helper
    - parent-side sibling snapshot helper
    - window-side visible rect snapshot helper for occlusion logic

- `RootClipSubtractVisibleWindowTree` at lines 632-654
  - Recurses through child windows while holding each window mutex.
  - Same issue as `FindWindow`: long recursive traversal under nested
    window locks.
  - Required cleanup:
    - per-window visibility/rect snapshot
    - child snapshot helper

- `BuildDesktopRootVisibleClipRegion` at lines 667-697
  - Takes `RootWindow->Mutex` and recurses into child trees from there.
  - Same cross-tree traversal issue.
  - Required cleanup:
    - root child snapshot helper
    - no recursion while owning the root lock

- `ShowWindow` at lines 735-772
  - Directly mutates `This->Style` and `This->Status` without any owner
    setter before taking `This->Mutex`.
  - Then recurses over children while holding the window lock.
  - This is both a data ownership violation and a long traversal under
    lock.
  - Required cleanup:
    - `WindowSetVisibleState(...)`
    - child snapshot helper
    - recursive propagation without holding the parent lock

- `GetWindowGC` at lines 1040-1089
  - Locks `Context->Mutex` while reading `This->ScreenRect`,
    `This->DrawOrigin`, `This->DrawClipRect`, and
    `This->DrawContextFlags` directly from the window without a window
    getter.
  - This is not the deadlock source, but it violates ownership.
  - Required cleanup:
    - window-side draw-context snapshot getter
    - context-side setters remain on the graphics-context owner

- `WindowHitTest` at lines 1584-1613
  - Recurses into child hit-tests while holding `This->Mutex`.
  - Another nested window traversal under lock.
  - Required cleanup:
    - child snapshot helper
    - window-side snapshot getter for hit-test state

### `kernel/source/desktop/Desktop-Cursor.c`

- `DesktopCursorBuildVisibleRegionForWindow` at lines 355-410
  - Reads `Window->ParentWindow` directly.
  - Takes `Parent->Mutex`, walks siblings, then takes child window mutexes
    recursively through `DesktopCursorSubtractVisibleWindowTreeFromRegion`.
  - Then takes `Window->Mutex` and walks child windows.
  - This is a full tree traversal coordinated from outside the owners.
  - Required cleanup:
    - `GetWindowParent(...)`
    - parent-side sibling snapshot helper
    - window-side child snapshot helper
    - window-side visible rect snapshot

- `DesktopCursorSubtractVisibleWindowTreeFromRegion` at lines 319-342
  - Recurses through children while keeping the current window mutex.
  - Same nested window traversal problem.

- `DesktopCursorRenderSoftwareOverlayOnWindow` at lines 837-922
  - Takes `Desktop->Mutex` to read cursor state.
  - Takes `Window->Mutex` to read screen rect.
  - Locks `GC->Mutex` directly and mutates clip fields.
  - This mixes three owners in one rendering path.
  - Required cleanup:
    - desktop cursor snapshot getter
    - window screen-rect getter
    - always use `SetGraphicsContextClipScreenRect(...)`

## Priority 2: Cross-owner tree traversals outside the hot draw path

### `kernel/source/desktop/Desktop-OverlayInvalidation.c`

- `DesktopOverlayInvalidateWindowTreeRectInternal` at lines 38-79
  - Takes one window mutex and recursively descends through children while
    still under that lock.
  - Calls `InvalidateWindowRect(...)` after collecting data.
  - Required cleanup:
    - window visibility/rect snapshot getter
    - child snapshot helper

- `DesktopOverlayInvalidateRootVisibleRemainderRect` at lines 90-152
  - Takes `RootWindow->Mutex`, reads root state, iterates child list, and
    reads child visibility and rect fields directly.
  - This is owner-crossing from the root lock into child-owned state.
  - Required cleanup:
    - root child snapshot helper
    - child visibility/rect snapshot getter

- `DesktopOverlayInvalidateRootRect` at lines 169-197
  - Reads root visibility and rect under a raw root window lock.
  - This should become a window-owned snapshot getter.

### `kernel/source/desktop/Desktop-Dispatcher.c`

- `DesktopAssignWindowTaskRecursive` at lines 61-77
  - Takes `Window->Mutex`, assigns `Window->Task`, then recurses through
    `Window->Children` while still holding the lock.
  - This is a long recursive subtree mutation under nested window locks.
  - Required cleanup:
    - window-side owner-task setter
    - child snapshot helper

### `kernel/source/desktop/Desktop-ThemeRuntime.c`

- `ThemeInvalidateWindowTree` at lines 130-147
  - Takes `Window->Mutex` and recursively descends through children while
    holding the current lock.
  - Required cleanup:
    - child snapshot helper
    - recursive invalidation outside the lock

### `kernel/source/desktop/Desktop-WindowClass.c`

- `WindowClassIsUsedByWindowTree` at lines 130-156
  - Takes `Window->Mutex`, reads `Window->Class`, and recursively descends
    into children while the lock is held.
  - Required cleanup:
    - window class getter
    - child snapshot helper

## Priority 3: Ownership violations that are simpler but still must be cleaned

### `kernel/source/desktop/Desktop-Draw.c`

- `ActivateWindowDrawContext` at lines 117-130
  - Directly writes window draw-context fields without an owner setter
    boundary.

- `ClearWindowDrawContext` at lines 138-145
  - Directly clears draw-context fields without an owner setter boundary.

- `DesktopGetWindowDrawSurfaceRect` at lines 155-161
  - Reads draw-context fields directly without a snapshot helper lock.

- `DesktopGetWindowDrawClipRect` at lines 172-178
  - Same issue.

These functions are already in the right subsystem, but they still need
one explicit owner API shape so callers never read or write draw-context
fields directly anywhere else.

### `kernel/source/desktop/Desktop-InternalTest.c`

- `FindDirectChildByID` at lines 194-208
  - Takes `RootWindow->Mutex` and walks children manually.
  - This should use the public child APIs already introduced.

- Desktop size probe at lines 340-347
  - Takes `RootWindow->Mutex` to read `ScreenRect`.
  - This should use a window snapshot getter.

## Priority 4: Owner-side locks that still exceed "fast get/set only"

These are not the most likely source of the `DeferredWork` /
`DesktopDispatcher` deadlock, but they still violate the target rule and
should be cleaned while the API surface is introduced.

### `kernel/source/desktop/Desktop-Timer.c`

- `DesktopTimerTask` at lines 126-144
  - Holds `Desktop->TimerMutex` while scanning the full timer list and
    updating due entries.
  - This should become a desktop-owned timer snapshot / due-extract helper.

- `SetWindowTimer` at lines 227-255
  - Holds `Desktop->TimerMutex` while searching the timer list and may
    allocate memory inside the critical section.
  - Allocation must move outside the lock.

- `KillWindowTimer` at lines 275-289
  - Holds `Desktop->TimerMutex` while traversing and removing entries.
  - This should be reduced to short owner-side list mutation steps.

- `DesktopTimerRemoveWindowTimers` at lines 303-315
  - Same pattern.

## Accessor/helper families required before refactor

The fixes above should converge on a small set of owner APIs instead of
ad-hoc local locking.

### Window-owned helpers

- `GetWindowScreenRectSnapshot(...)`
- `GetWindowRectSnapshot(...)`
- `GetWindowVisibilitySnapshot(...)`
- `GetWindowOrderSnapshot(...)`
- `GetWindowEffectiveWorkRectSnapshot(...)`
- `GetWindowDrawContextSnapshot(...)`
- `ConsumeWindowDirtyRegionSnapshot(...)`
- `SetWindowVisibleState(...)`
- `SetWindowOwnerTask(...)`
- `AttachChildWindow(...)`
- `DetachChildWindow(...)`
- `ReorderParentChildToFront(...)`
- `SnapshotWindowChildren(...)`
- `SnapshotWindowSiblings(...)`

### Desktop-owned helpers

- `GetDesktopScreenRect(...)` without nested window lock in the caller
- `GetDesktopCursorStateSnapshot(...)`
- `SetDesktopCursorState(...)` split into small owner mutations
- `GetDesktopCaptureState(...)`
- `SetDesktopCaptureState(...)`
- `ClearDesktopWindowReferences(...)` for capture/focus cleanup on delete

### Graphics-context-owned helpers

- keep `SetGraphicsContextClipScreenRect(...)`
- add `SetGraphicsContextOrigin(...)` if direct origin writes remain
- forbid direct `GC->Mutex` use from callers

## Suggested refactor order

1. Remove all `Desktop->Mutex` then `Window->Mutex` nesting.
2. Remove all recursive window-tree traversals that keep one window lock
   while descending into child windows.
3. Replace direct `Status` / `Style` / `Task` / `DrawContext` field
   mutations with owner-side setters.
4. Replace direct child list iteration with owner-side snapshot helpers.
5. Replace direct graphics-context mutex usage with context setters.

## Most likely deadlock contributors

If the goal is to target the live freeze first, the first places to fix
are:

- `kernel/source/desktop/Desktop-Main.c:GetDesktopScreenRect`
- `kernel/source/desktop/Desktop-Graphics.c:BuildWindowDrawClipRegion`
- `kernel/source/desktop/Desktop-Graphics.c:BuildDesktopRootVisibleClipRegion`
- `kernel/source/desktop/Desktop-Cursor.c:DesktopCursorBuildVisibleRegionForWindow`
- `kernel/source/desktop/Desktop-OverlayInvalidation.c:*`

Those paths sit directly on desktop draw, cursor damage, and visible-tree
clipping, which matches the log timing right after the first desktop draw.
