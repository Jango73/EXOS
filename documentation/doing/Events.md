# Event-Driven Task Messages

1. [X] Inventory current message system and keyboard path
   - Map existing window message structures/functions (SendMessage, PostMessage, PeekMessage, GetMessage, queues, init/destroy, synchronization).
   - Trace keyboard flow (ISR/driver → dispatch → window) to locate injection points and polling loops (e.g., shell).
2. [X] Define unified MessageQueue abstraction
   - Specify common queue struct and ops (init/destroy, push/pop/peek, wait/wakeup, capacity/limits) usable by both windows and tasks.
   - Align message format for task delivery (key codes, flags, optional sender).
3. [X] Extend API contract to handle NULL handles for tasks
   - Allow SendMessage/PostMessage/PeekMessage/GetMessage to accept NULL as “target task” (no window).
   - Adjust validation to accept NULL handles and route accordingly.
4. [X] Attach MessageQueue to Task lifecycle
   - Add MessageQueue field to Task; create during task creation and destroy at teardown.
   - Reuse the same queue implementation for windows (via composition/wrappers) to maximize shared code.
5. [X] Implement message injection to tasks
   - Update dispatch so NULL handle posts into the target task’s queue (current task or explicit task when provided).
   - Adapt keyboard ISR/driver path to post key events to tasks without windows (e.g., shell) using PostMessage with NULL handle.
   - Ensure SAFE_USE/SAFE_USE_VALID_ID use on kernel object pointers where required.
6. [X] Consume messages in tasks
   - Extend PeekMessage/GetMessage to read from the calling task’s queue when handle is NULL.
   - Update shell and other windowless apps to replace keyboard polling loops with PeekMessage/GetMessage handling key messages.
7. [X] Adapt Edit.c to the event system
   - Replace keyboard polling with PeekMessage/GetMessage handling EWM_KEYDOWN.
   - Preserve existing navigation/editing behavior.
8. [X] Synchronization and wakeup
   - Ensure queue ops are thread-safe (locks/irq masking) and that GetMessage can block; wake the task when a message is enqueued.
   - Keep PeekMessage non-blocking; confirm scheduler interaction for wakeups is correct.
9. [X] Compatibility, tracing, and docs
   - Verify window message delivery remains unchanged; add DEBUG logs on new task message paths for diagnostics.
   - Add/update documentation in documentation/Kernel.md describing shared message queues, NULL-handle semantics, and task lifecycle notes.

# Step 1 findings (inventory)

- Message objects/queue: `kernel/source/process/Task.c` defines `MESSAGE` helpers (`NewMessage`, `DeleteMessage`, `MessageDestructor`) and per-task queue: `TASK` has `MessageMutex` + `Message` (list), created in `NewTask()` via `NewList`, destroyed in `DeleteTask()`.
- Posting/delivery: `PostMessage(HANDLE, Msg, Param1, Param2)` (Task.c) requires non-NULL Target; routes to tasks by pointer equality or windows (current desktop lookup). For windows, dedupes `EWM_DRAW`. When a target task is waiting (`TASK_STATUS_WAITMESSAGE`), it is set back to RUNNING. `SendMessage` is synchronous window-only in current process desktop. `DispatchMessage` routes only to windows. `SysCall_*` wrappers translate handles; `SysCall_PeekMessage` is stubbed.
- Retrieval/waiting: `GetMessage` (Task.c) works on current task queue, blocking via `WaitForMessage` (sets status to `TASK_STATUS_WAITMESSAGE` and spins with `IdleCPU`). Optional filter by `Message->Target`; `Message->Target==NULL` pops FIFO. `PeekMessage` not implemented in kernel/syscall.
- Keyboard path (polling): `drivers/Keyboard.c` ISR (`KeyboardHandler` → `HandleScanCode`) fills `Keyboard.Buffer` with `SendKeyCodeToBuffer`; state exposed via `PeekChar`, `GetKeyCode`, `GetChar`, and syscalls `SysCall_ConsolePeekKey`/`SysCall_ConsoleGetKey`. No posting into message queues.
- Polling consumers: Shell input uses `CommandLineEditorReadLine` (`kernel/source/utils/CommandLineEditor.c`) with `FOREVER` loop polling `PeekChar`/`GetKeyCode`; similar polling appears in other utilities (`MemoryEditor.c`, `Console.c`, `Edit.c`, etc.). Currently no event-driven consumption for tasks without windows.

# Step 2 design (unified MessageQueue abstraction)

- Queue struct: introduce shared `MESSAGEQUEUE` holding `LPLIST Messages`, `MUTEX Mutex`, optional `UINT Flags`/`Capacity`, and a waiter indicator (e.g., `BOOL Waiting` or count) to wake tasks. Owned by TASK and by window/desktop wrappers.
- Initialization/lifecycle: `MessageQueueInit(MessageQueue*, AllocFn)` to set mutex + list (using existing `NewList`/`MessageDestructor`); `MessageQueueDestroy` to delete list. Task creation/teardown calls these; window contexts reuse the same helper instead of custom setup in `NewTask`.
- Enqueue path: `MessageQueueEnqueue(queue, targetHandle, msgId, p1, p2, dedupeFlags)` creates `MESSAGE`, sets fields/time, and pushes to tail. Allow dedupe hook for `EWM_DRAW`-style messages to reuse existing entry. Returns BOOL for success/failure.
- Dequeue/peek: `MessageQueuePeek(queue, filterHandle, out Message)` is non-blocking; leaves entry in place. `MessageQueueDequeue(queue, filterHandle, out Message)` pops FIFO or first matching target (mirrors current `GetMessage` behavior). Both operate under the queue mutex only.
- Wait/wakeup: `MessageQueueWait(queue, Task)` sets task to `TASK_STATUS_WAITMESSAGE` and blocks (current `WaitForMessage` semantics). `MessageQueueSignal(queue)` wakes a waiting task (sets status RUNNING) when enqueue succeeds. Keeps scheduler interaction centralized.
- Message format alignment: retain `MESSAGE` layout (Target, Time, Message, Param1, Param2). Standardize keyboard-to-task payload: `Param1` virtual key or ASCII code, `Param2` modifier/flags, so dispatchers can share mapping.
- Integration intent: TASK embeds `MESSAGEQUEUE`; window message queues become the same queue filtered by handle. `PostMessage`/`GetMessage`/`PeekMessage` wrappers call these helpers, eliminating duplicate mutex logic and allowing NULL-handle task messages without special-case code.

# Step 3 API contract (NULL handle support)

- Semantic: `Target == NULL` means “message directed to the calling task (no window)” for `PostMessage`, `SendMessage`, `PeekMessage`, `GetMessage`. No window lookup is performed; the task’s own queue is used.
- Validation: kernel/userland entry points must accept `Target == NULL` as valid; only reject when other invariants fail (bad pointers, etc.). `SysCall_*` wrappers keep handles as-is when NULL.
- Dispatch rules:
  - `PostMessage(NULL, Msg, p1, p2)`: enqueue into current task’s queue; wake if waiting.
  - `SendMessage(NULL, ...)`: synchronous call is a no-op (or returns 0) because there is no window handler; keep behavior documented.
  - `PeekMessage(NULL, ...)`/`GetMessage(NULL, ...)`: operate on current task queue with no window filter.
  - Non-NULL handles keep current behavior (window/task routing as today).
- Message filtering: `MESSAGE.Target` stays meaningful—when enqueued with NULL, stored Target can remain NULL or be set to `EnsureHandle(CurrentTask)` if we need identity; document choice when implemented (default: keep NULL to indicate task-scope).
- Error codes: maintain existing return types (BOOL/U32). On invalid non-NULL handle, return FALSE/0; on NULL, treat as valid and proceed.

# Step 4 implementation (Task lifecycle wiring)

- Added shared `MESSAGEQUEUE` struct (mutex, list, flags/capacity, waiter marker) in `kernel/include/process/Task.h`.
- Introduced `InitMessageQueue`/`DeleteMessageQueue` helpers and wired `NewTask`/`DeleteTask` to initialize/tear down the queue; `NewTask` now fails cleanly if queue allocation fails.
- Task message operations now use the embedded queue (`MessageQueue.Mutex` + `MessageQueue.Messages`) instead of ad-hoc `MessageMutex`/`Message` fields; window message posting still reuses the owning task queue via the same helpers.

# Step 5 implementation (message injection to tasks)

- NULL-target posting: `PostMessage` now accepts `Target == NULL`, enqueues into the current task’s message queue, and wakes it if waiting; syscall wrappers (`SysCall_PostMessage`/`SendMessage`) pass NULL through instead of rejecting.
- Task handle support: `PostMessage` resolves task targets (handle → pointer in syscall) and queues to the matching task.
- Keyboard dispatch: keyboard driver now broadcasts `EWM_KEYDOWN` to all tasks via `BroadcastMessage` (virtual key in `Param1`, ASCII in `Param2`); legacy buffer remains for polling clients.
- Broadcast helper: added `BroadcastMessage` in `Task.c`, sending messages to every task and waking waiters.
- Queue constraints: message queue now lazily initializes on first use; queues are capped at 100 pending messages (drop with warning on overflow). `NewTask` no longer pre-allocates the queue.

# Step 6 implementation (task-side consumption)

- Implemented `PeekMessage` in kernel and syscall/runtime layers; it reads from the caller’s task queue (non-blocking) with optional target filter.
- Shell input now consumes `EWM_KEYDOWN` via `GetMessage` in `CommandLineEditorReadLine`, replacing keyboard polling.

# Step 7 implementation (Edit.c event handling)

- Editor main loop now blocks on `GetMessage` and processes `EWM_KEYDOWN` events (VirtualKey in Param1, ASCII in Param2), removing keyboard polling.
- Menu shortcuts and editing/navigation logic preserved; modifier checks still use existing helper functions.

# Step 8 implementation (synchronization/wakeup)

- Added queue-level waiting flag: `WaitForMessage` marks the queue as waiting and clears it after wake; `AddTaskMessage` checks the flag to wake tasks and reset it.
- Preserved lock ordering (task mutex then queue mutex) and kept `PeekMessage` non-blocking; `GetMessage` still blocks via `WaitForMessage` but now cooperates with the queue flag to avoid missed wakeups.

# Step 9 implementation (compatibility/tracing/docs)

- Documented the task/window message flow in `documentation/Kernel.md`, covering queue lifecycle, limits, posting rules (NULL/task/window), broadcast key events, and Peek/Get behavior.
