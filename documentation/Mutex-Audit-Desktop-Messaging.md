# Desktop, Graphics, and Messaging Mutex Audit

## Scope

This audit covers the mutex behavior itself and the lock usage around:
- desktop and window tree management
- desktop graphics and drawing paths
- task and process messaging

The windowing lock contract is declared in [Desktop.h](../kernel/include/desktop/Desktop.h) and forbids:
- calling `PostMessage` while holding desktop tree/state/window mutexes
- calling window callbacks while holding desktop tree/state/window mutexes

## Mutex Core

### Removed destructive recovery

- `kernel/source/Mutex.c:137`
  `LockMutex()` used to force-release a recursively held mutex after a timeout. That behavior has been removed.
- `kernel/source/Mutex.c:307`
  `UnlockMutex()` still relies on strict owner/task matching and no longer races with forced ownership reset.

### Remaining core risk

- `kernel/source/Mutex.c:137`
  The primitive still uses `INFINITY` heavily across the kernel. This preserves deadlock potential when lock ordering is violated.

## Forbidden Callback Dispatch Under Window Mutex

These sites violate the declared contract because they execute `Window->Function` while `Window->Mutex` is held.

- `kernel/source/process/Task-Messaging.c:823`
  `SendMessage()` resolves the target, then locks `Window->Mutex` and calls `Window->Function`.
- `kernel/source/process/Task-Messaging.c:999`
  `DispatchMessage()` does the same for queued dispatch.

Impact:
- callback code can re-enter window helpers that re-lock the same window
- callback code can call message APIs or desktop helpers while the window lock is still held
- deadlock risk increases when another path takes parent/tree/message locks and then waits on the same window

## Parent/Child Window Lock Order Inversions

These paths lock windows in opposite directions.

### Parent to child recursion

- `kernel/source/desktop/Desktop-Main.c:678`
  `FindWindow()` locks the parent window and recurses into children.
- `kernel/source/desktop/Desktop-Graphics.c:1579`
  `WindowHitTest()` locks the parent window and recurses into children.
- `kernel/source/desktop/Desktop-Graphics.c:631`
  `RootClipSubtractVisibleWindowTree()` locks a window and recurses into children.
- `kernel/source/desktop/Desktop-Graphics.c:683`
  `BuildDesktopRootVisibleClipRegion()` locks the root window, then descends into child traversal.

### Child to parent acquisition

- `kernel/source/desktop/Desktop-Graphics.c:349`
  `DefaultSetWindowRect()` locks the child window first, then calls parent-based clamping and follow-up invalidation work.
- `kernel/source/desktop/Desktop-Main.c:608`
  `DeleteWindow()` locks the child window, then locks the parent window to remove the child from the list.
- `kernel/source/desktop/Desktop-Graphics.c:452`
  `BuildWindowDrawClipRegion()` locks the child window, releases it, then locks the parent and descends into sibling subtrees.

Impact:
- parent->child and child->parent coexist
- any concurrent traversal plus mutation can deadlock under `INFINITY`

## PostMessage While Holding Structural or Object Locks

These sites call `PostMessage()` immediately after or around window state mutation and are sensitive to re-entry. They are not all direct contract violations, but they are dangerous because the surrounding flow still depends on the mutated object graph.

- `kernel/source/desktop/Desktop-Graphics.c:349`
  `DefaultSetWindowRect()` mutates window geometry and then posts `EWN_WINDOW_RECT_CHANGED`.
- `kernel/source/desktop/Desktop-Main.c:1184`
  `RequestWindowDraw()` sets `WINDOW_STATUS_NEED_DRAW` and posts `EWM_DRAW`.
- `kernel/source/desktop/Desktop-Graphics.c:957`
  `SetWindowProp()` mutates the property list and posts `EWN_WINDOW_PROPERTY_CHANGED`.
- `kernel/source/desktop/Desktop-Timer.c:147`
  Timer delivery posts `EWM_TIMER` for due entries.

Impact:
- queueing itself is asynchronous, but callbacks can consume the message before surrounding subsystems reach a stable state if object ownership or tree state is still being rearranged elsewhere

## Recursive Window Lock Usage During Draw Dispatch

These sites re-lock the same window during a draw callback that is already invoked under `Window->Mutex`.

- `kernel/source/desktop/Desktop-WindowFunc.c:138`
  `DefaultWindowFunc(EWM_DRAW)` locks `This->Mutex` to toggle draw flags.
- `kernel/source/desktop/Desktop-Graphics.c:1795`
  `DesktopWindowFunc(EWM_DRAW)` also locks the same window to toggle draw flags.

Impact:
- recursive locking becomes part of the normal path
- any mutex policy that treats prolonged recursion as a recoverable error will corrupt ownership

## Message Queue Paths With Nested Task Locking

These paths hold `Task->Mutex` and call helpers that take the same lock again.

- `kernel/source/process/Task-Messaging.c:442`
  `AddTaskMessage()` locks `Task->Mutex` and `Task->MessageQueue.Mutex`.
- `kernel/source/process/Task-Messaging.c:472`
  Inside that lock scope, it calls `GetTaskStatus()` and `SetTaskStatus()`.
- `kernel/source/process/Task.c:767`
  `GetTaskStatus()` locks `Task->Mutex`.
- `kernel/source/process/Task.c:789`
  `SetTaskStatus()` locks `Task->Mutex`.

Impact:
- recursive task locking becomes routine in the messaging wakeup path
- lock duration expands because scheduler state changes happen from inside nested lock scopes

## Long or Blocking Operations Under High-Level Locks

These sites hold broad locks while calling operations that can block or wait on hardware.

- `kernel/source/desktop/Desktop-Main.c:394`
  `ShowDesktop()` holds `MUTEX_KERNEL` and `Desktop->Mutex`.
- `kernel/source/desktop/Desktop-Main.c:459`
  Inside that scope it calls `DF_GFX_SETMODE`.
- `kernel/source/desktop/Desktop-Main.c:509`
  Still inside the same scope it calls `DisplaySessionSetDesktopMode()`.

Impact:
- graphics mode setup can wait on hardware state or driver timeouts
- broad locks amplify the blast radius of a slow or stuck graphics path

## Object Release While Mutex Remains Held

These sites release kernel objects without unlocking the object mutex first.

- `kernel/source/desktop/Desktop-Main.c:368`
  `DeleteDesktop()` locks `This->Mutex` and calls `ReleaseKernelObject(This)` without unlocking.
- `kernel/source/desktop/Desktop-Main.c:608`
  `DeleteWindow()` locks `This->Mutex` and calls `ReleaseKernelObject(This)` without unlocking.

Impact:
- waiters can observe freed or recycled memory while still treating the mutex as live
- teardown ordering becomes undefined

## Recursive Show/Tree Walk Under Window Mutex

- `kernel/source/desktop/Desktop-Graphics.c:734`
  `ShowWindow()` locks a window and recursively calls `ShowWindow()` on visible children while still holding the parent mutex.

Impact:
- parent lock stays held across subtree recursion
- any child path that needs parent state in the opposite order can deadlock

## Recommended Direction

1. Never recover from a deadlock by mutating mutex ownership.
2. Do not call `Window->Function` while holding `Window->Mutex`.
3. Use one canonical order for window hierarchy locking.
4. Replace recursive structural walks with snapshots where practical.
5. Keep driver and graphics mode operations outside broad desktop and kernel locks.
6. Add lock-role checking to desktop and graphics direct `LockMutex()` call sites, not only to messaging wrappers.
