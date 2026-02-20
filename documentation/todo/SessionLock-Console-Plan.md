# Session Lock (Console first, generic core) - Integration Plan

## Objectives
1. Reuse the existing session inactivity timeout concept for automatic lock.
2. Make the timeout configurable from `exos.ref.toml` with a compile-time fallback constant.
3. Lock only when a session exists and the current user has a password configured.
4. Show a lock screen that supports:
   - unlock with current user password
   - switch user (login as another user)
5. Restore the previous working screen after successful unlock.
6. Implement only the console path now, with a generic mechanism ready for Window Manager integration later.

## Scope And Non-Scope
1. In scope:
   - kernel session lock state management
   - console lock screen flow
   - inactivity tracking wiring in shell input path
   - screen capture/restore for console
2. Out of scope (this phase):
   - graphical lock screen and window manager integration
   - policy/UI theming for desktop mode

## Design Decisions
1. The existing inactivity timeout used by `ValidateUserSession()` becomes the lock trigger policy (same concept, no second timeout policy).
2. Session expiration by inactivity is replaced by session lock state; session object stays alive.
3. Lock behavior is policy/core; rendering and input are backend-specific.
4. Console implementation is the first backend and reference implementation.

## Proposed Kernel Architecture
1. Add a session lock core component (`SessionLock`) responsible for:
   - lock state per session
   - inactivity checks
   - unlock attempt validation
   - backend-neutral lock/unlock lifecycle
2. Define a backend interface with function pointers:
   - `CaptureState`
   - `ShowLockScreen`
   - `RunUnlockLoop`
   - `RestoreState`
3. Register console backend during shell initialization.
4. Later, register GFX backend from desktop/window manager with the same interface.

## Data Model Changes (planned)
1. Extend `USERSESSION` with lock metadata:
   - `BOOL IsLocked`
   - `U32 LockReason` (timeout/manual, optional)
   - `DATETIME LockTime`
   - `U32 FailedUnlockCount`
2. Keep `LastActivity` as single source for inactivity checks.
3. Keep `SESSION_TIMEOUT_MS` fallback constant, and add config override loading.

## Configuration
1. Keep compile fallback in code:
   - existing `SESSION_TIMEOUT_MS` logic as default
2. Add override in `kernel/configuration/exos.ref.toml`:
   - `Session.TimeoutMs=<value>`
3. At boot/config-apply time:
   - parse `Session.TimeoutMs`
   - if invalid or absent, keep fallback
4. Compatibility alias:
   - `Session.TimeoutMinutes` is accepted, converted to milliseconds

## Console Flow (phase 1)
1. During shell input loop, detect inactivity timeout or inside input wait loop.
2. If timeout reached and session eligible for lock:
   - mark session locked
   - capture console state
   - clear/paint lock screen
   - run unlock interaction loop
3. Unlock options:
   - current user password prompt
   - switch user -> login flow -> replace current session binding
4. On successful unlock:
   - clear lock overlay
   - restore captured console screen and cursor
   - refresh prompt/input context
   - update activity timestamp

## Console State Restore Strategy
1. Add a small console snapshot utility dedicated to lock/unlock:
   - text mode: copy full `Console.Memory` buffer + cursor/attributes
   - framebuffer console mode: copy visible cell buffer abstraction (or framebuffer region + cursor), then restore
2. Save before lock UI draw, restore only after successful unlock.
3. Ensure mutex protection (`MUTEX_CONSOLE`) during capture/restore.

## Activity Tracking Wiring
1. Call `UpdateSessionActivity()` when meaningful user input is received:
   - printable key insertion
   - control/edit keys used in line editor
   - command execution completion
2. Do not update activity while locked except on successful unlock, to avoid extending lock forever.

## Error Handling And Security Notes
1. If no active session: no lock.
2. If active user has no password hash: no lock (as requested).
3. Mask password input (`CommandLineEditorReadLine(..., TRUE)`).
4. Add unlock attempt logging with flood protection if needed (`RateLimiter`).
5. Keep lock screen isolated from previous command buffer content.

## Target Files (expected implementation phase)
1. `kernel/include/UserSession.h`
2. `kernel/include/UserAccount.h`
3. `kernel/source/UserSession.c`
4. `kernel/source/shell/Shell-Main.c`
5. `kernel/source/utils/CommandLineEditor.c`
6. `kernel/include/utils/Helpers.h` (config key constant)
7. `kernel/source/Kernel.c` (config apply)
8. `kernel/configuration/exos.ref.toml`
9. `kernel/source/Console.c` or dedicated console lock helper files

## Validation Checklist
1. Active session + inactivity timeout -> lock screen appears.
2. Wrong password -> stays locked.
3. Correct password -> screen restored exactly.
4. Switch user from lock screen -> new session active, shell continues.
5. No session -> no lock.
6. Session with no password -> no lock.
7. Works in both x86-32 and x86-64 console boots.
