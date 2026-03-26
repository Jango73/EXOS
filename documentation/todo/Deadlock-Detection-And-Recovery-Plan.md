# Deadlock Detection And Recovery Plan

## Goal

Add a generic kernel-wide deadlock detection mechanism for mutex waits, plus a controlled recovery policy that does not corrupt protected state.

The implementation must:
- live in reusable `utils` code, not in one subsystem
- work for any kernel mutex user
- detect real wait cycles between tasks and mutex owners
- produce short actionable diagnostics
- avoid unsafe generic recovery such as force-unlocking mutexes

## Problem Statement

The kernel has blocking mutex waits but no generic runtime detection of:
- `Task A` waiting on a mutex held by `Task B`
- while `Task B` is itself waiting on another mutex or blocking path that leads back to `Task A`

Without a shared detector:
- deadlocks remain intermittent and expensive to diagnose
- subsystem-local instrumentation has to be re-added every time
- lock-order comments are not enough to identify runtime wait cycles

## Design Principles

- Detection must be generic and centralized.
- Mutex ownership remains authoritative in `Mutex`.
- Task wait state remains authoritative in scheduler/task code.
- Diagnostics must explain the cycle in task/mutex terms.
- Recovery must preserve correctness before convenience.
- No subsystem-specific deadlock code in desktop, process, driver, or filesystem paths.

## Scope

Initial scope:
- mutex-based deadlock detection
- task-to-mutex wait chain tracking
- warning and debug-stop reactions
- optional abort of explicitly interruptible waits

Out of scope for the first implementation:
- semaphore deadlocks
- message queue logical deadlocks without mutex waits
- forceful recovery by unlocking mutexes
- automatic task termination as a generic policy

## Target Model

Represent runtime blocking with a wait-for graph:
- `Task -> Mutex` when a task starts waiting in `LockMutex`
- `Mutex -> OwnerTask` when the mutex is owned

Cycle example:
- `Task A -> Mutex X -> Task B -> Mutex Y -> Task A`

When a task begins a blocking wait:
1. record the waited mutex on the task
2. read the current owner of that mutex
3. walk the owner chain through waited mutexes
4. if the walk returns to the original task, report a deadlock

This model is sufficient for classic mutex cycles and keeps the implementation small.

## Proposed Components

## 1. `utils/DeadlockMonitor`

Create:
- `kernel/include/utils/DeadlockMonitor.h`
- `kernel/source/utils/DeadlockMonitor.c`

Responsibilities:
- register wait start
- register wait end
- register ownership changes
- detect cycles
- emit a compact diagnostic chain
- expose policy helpers for warning vs debug stop vs abortable wait

Suggested public functions:
- `void DeadlockMonitorOnWaitStart(LPTASK Task, LPMUTEX Mutex);`
- `void DeadlockMonitorOnWaitCancel(LPTASK Task, LPMUTEX Mutex);`
- `void DeadlockMonitorOnAcquire(LPTASK Task, LPMUTEX Mutex);`
- `void DeadlockMonitorOnRelease(LPTASK Task, LPMUTEX Mutex, LPTASK NextOwner);`
- `BOOL DeadlockMonitorWouldCreateCycle(LPTASK Task, LPMUTEX Mutex);`

## 2. Task wait tracking

Extend `TASK` with minimal deadlock-tracking state:
- `LPMUTEX WaitingMutex`
- `UINT WaitingSince`

Optional debug-only fields:
- `LPCSTR WaitingReason`
- `U32 DeadlockIncidentCount`

This data must reflect the current blocking wait only and be cleared as soon as the wait ends.

## 3. Mutex debug identity

Extend `MUTEX` with optional debug metadata:
- `LPTASK OwnerTask` if not already authoritative enough in current implementation
- `LPCSTR DebugName`
- `U32 DebugClass`

`DebugName` and `DebugClass` are diagnostic aids only.

Lock classes are optional in the first phase, but the structure should leave room for them so order-prevention checks can be added later without redesign.

## Hook Points

## `Mutex.c`

Primary hook points:
- just before a task enters a blocking wait in `LockMutex`
- immediately after a blocked task acquires the mutex
- on timeout / cancellation / aborted wait
- on unlock when ownership changes

Required behavior:
- never leave stale `Task->WaitingMutex`
- never emit the same deadlock warning in a tight flood loop
- keep the monitor side effects minimal while interrupts are disabled

## `Schedule.c` or task status helpers

Only add scheduler hooks if needed to keep `WaitingSince` or wait-state cleanup coherent.

The deadlock detector should not depend on scheduler policy logic for its correctness if `Mutex.c` can maintain the full mutex wait state by itself.

## Step 1 Audit Result

Mutex ownership semantics are already clear enough for the first deadlock-monitor pass:
- authoritative mutex owner: `Mutex->Task`
- owner process mirror: `Mutex->Process`
- recursive ownership: represented by `Mutex->Lock > 1`

`LockMutex` blocking behavior is concentrated in one place:
- if `Mutex->Task == CurrentTask`, the lock is recursive and returns immediately after incrementing `Mutex->Lock`
- otherwise the caller loops until `Mutex->Task == NULL`, with timeout handling and a sleep/wake retry loop inside `LockMutex`
- once the mutex becomes free, ownership is assigned by writing `Mutex->Process`, `Mutex->Task`, and `Mutex->Lock = 1`

`LockMutex` exits that matter for future deadlock-monitor cleanup:
- immediate recursive success
- waited success after the blocking loop
- timeout return
- invalid mutex return
- forced break caused by the existing reentrant-hold recovery path

`UnlockMutex` is the authoritative ownership release path:
- only the owning task may unlock
- it decrements `Mutex->Lock`
- when `Mutex->Lock` reaches `0`, it clears `Mutex->Process` and `Mutex->Task`

Scope boundary confirmed:
- the first detector pass should cover mutex deadlocks only
- `Wait()` is a separate multi-object wait mechanism and must not be mixed into the first mutex deadlock model
- a single `Task->WaitingMutex` is therefore valid for the first pass because one task can only be blocked on one `LockMutex()` at a time

## Detection Algorithm

At `DeadlockMonitorOnWaitStart(Task, Mutex)`:
1. store `Task->WaitingMutex = Mutex`
2. set `Task->WaitingSince`
3. get `Owner = Mutex->OwnerTask`
4. if `Owner == NULL`, return
5. walk:
   - current task = `Owner`
   - current waited mutex = `CurrentTask->WaitingMutex`
   - next owner = `CurrentWaitedMutex->OwnerTask`
6. stop when:
   - the chain ends
   - an invalid object is found
   - a loop limit is reached
   - the original waiting task is reached again
7. if the original task is reached, a deadlock exists

Loop walk rules:
- use a bounded maximum depth
- validate pointers with the usual kernel object guards
- log the full chain once the cycle is confirmed

## Reaction Policy

## Normal builds

Default reaction:
- emit one `WARNING()` or `ERROR()` with a short summary
- emit `DEBUG()` lines for the full chain
- keep a rate-limited global counter

This gives diagnostics without changing normal control flow.

## Debug builds

Default reaction:
- emit the same diagnostic
- stop in a fail-fast way once a cycle is confirmed

Possible mechanisms:
- dedicated panic path
- debug break
- controlled kernel halt

The exact mechanism should follow existing kernel debug-failure conventions.

## Interruptible waits

Optional second-phase behavior:
- add a lock call mode that allows aborting the wait when a deadlock is detected
- return a dedicated status to the caller

This is the only generic automatic recovery that is safe enough to consider.

## Forbidden Recovery Policies

Do not implement these as generic deadlock recovery:
- force `UnlockMutex` on a mutex held by another task
- automatic task kill
- automatic process kill
- ad-hoc subsystem rollback from inside the deadlock monitor

These actions can leave protected state partially mutated and make the system less diagnosable than a controlled stop.

## Diagnostics

Human-facing message requirements:
- short summary in `WARNING()` or `ERROR()`
- task names and pointers
- mutex name/class if available
- whether the incident was detected in warn mode or fail-fast mode

Detailed chain should go to `DEBUG()`, for example:
- `Task A waiting for Mutex X owned by Task B`
- `Task B waiting for Mutex Y owned by Task C`
- `Task C waiting for Mutex Z owned by Task A`

All log strings must follow normal kernel logging rules.

## Implementation Plan

## [x] Step 1 - Audit and freeze mutex ownership semantics

- Confirm the authoritative owner field and wait path in `Mutex.c`.
- Confirm all exits from `LockMutex` that must clear wait state.
- Confirm whether any mutex path can sleep without going through the normal blocking loop.

Success criteria:
- there is one clear ownership model and one clear place to hook wait start/end.

## [x] Step 2 - Add minimal task wait tracking

- Extend `TASK` with `WaitingMutex` and `WaitingSince`.
- Initialize and clear the fields in all task lifecycle paths.
- Keep the fields valid only while the task is blocked on a mutex.

Success criteria:
- every blocking mutex wait leaves an observable task-side wait target.

## [ ] Step 3 - Introduce `DeadlockMonitor`

- Add the reusable module in `kernel/include/utils` and `kernel/source/utils`.
- Implement bounded owner-chain walking.
- Implement cycle detection against the original waiter.
- Keep the module independent from desktop/process/filesystem code.

Success criteria:
- the module can answer whether one new wait would create a cycle.

## [ ] Step 4 - Hook `Mutex.c`

- Call the monitor on wait start.
- Call the monitor on acquire after blocking.
- Call the monitor on timeout or canceled wait.
- Call the monitor on release if ownership bookkeeping needs refresh.

Success criteria:
- mutex waits update the monitor state correctly in success, timeout, and abort paths.

## [ ] Step 5 - Add diagnostics

- Add one short human-facing deadlock summary.
- Add detailed `DEBUG()` chain logging.
- Add rate limiting to avoid log floods on repeated detection.

Success criteria:
- one deadlock incident yields one readable diagnostic sequence.

## [ ] Step 6 - Add debug fail-fast policy

- In debug builds, stop the kernel when a cycle is confirmed.
- Keep normal builds in diagnostic-only mode.

Success criteria:
- a confirmed deadlock is immediately visible and reproducible in debug runs.

## [ ] Step 7 - Optional interruptible wait support

- Add an opt-in lock mode or lock helper that can return failure when deadlock is detected.
- Restrict it to callers that can safely unwind.

Success criteria:
- selected callers can fail cleanly instead of hanging forever.

## [ ] Step 8 - Add lock classes for prevention diagnostics

- Add optional mutex classes and names.
- Track held lock classes per task.
- warn on class-order violations at acquisition time

This is a prevention layer, not a replacement for runtime cycle detection.

Success criteria:
- incompatible acquisition orders are reported before they become hard deadlocks.

## Validation Plan

Required validation:
- self-deadlock attempt on one mutex if recursive locking is forbidden
- two-task / two-mutex cycle
- three-task / three-mutex cycle
- timeout path clears wait state
- successful wakeup clears wait state
- no false positive on normal contention
- no flood when the same cycle repeats

Recommended validation support:
- a dedicated kernel autotest or debug shell command that constructs controlled deadlock scenarios
- log assertions on the expected deadlock chain

## Open Questions

- Should the first implementation log through `WARNING()` or `ERROR()` for confirmed cycles in non-debug builds?
- Does `Mutex` already expose enough owner identity, or does it need an explicit `OwnerTask` field cleanup pass?
- Which callers are safe candidates for future interruptible deadlock-abort behavior?
- Is lock-class prevention wanted immediately, or after the runtime detector is validated?
