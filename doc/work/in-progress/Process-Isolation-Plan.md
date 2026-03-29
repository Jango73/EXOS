# Process Isolation Policy Based On User Ownership

## Goal

Put process isolation based on user ownership on a clean foundation with the minimum useful change set:

- one user process can only act on its own processes
- administrator and kernel contexts can act on all processes
- identity resolution must be centralized
- the same rule must be reused by expose, syscalls, and shell paths

## Scope

This plan intentionally stays narrow:

- no file ACL work
- no audit trail work
- no persistence changes
- no broad redesign of the security model

## Minimal Target Policy

- Effective process owner comes from `Process->Session->UserID` when a session exists.
- Otherwise effective process owner comes from `Process->UserID`.
- If no reliable owner is available, access is denied by default.
- `EXOS_PRIVILEGE_ADMIN` and kernel privilege bypass same-user restrictions.
- Regular users can target only processes owned by the same user.

## Implementation Plan

### 1. Stabilize `PROCESS.UserID`

Make `PROCESS.UserID` a real owner field instead of a mostly unused placeholder.

- Set `PROCESS.UserID` during process creation.
- If the parent process has a session, copy `Parent->Session->UserID`.
- Otherwise, inherit `Parent->UserID` when available.
- Keep `PROCESS.Session` as the live authentication context.
- Keep `PROCESS.UserID` as the stable ownership fallback.

Result:

- process ownership remains available even when session state is absent or changes later

### 2. Add one reusable policy helper

Create one small shared module in:

- `kernel/include/utils`
- `kernel/source/utils`

Proposed API:

```c
BOOL ProcessAccessCanTargetProcess(LPPROCESS Caller, LPPROCESS Target, BOOL AllowAdminOverride);
BOOL ProcessAccessIsSameUser(LPPROCESS Caller, LPPROCESS Target);
U64 ProcessAccessGetEffectiveUserID(LPPROCESS Process);
```

Responsibilities:

- resolve effective owner from session or `PROCESS.UserID`
- compare ownership
- apply admin/kernel override
- deny on missing identity

Result:

- one source of truth for same-user process policy

### 3. Reuse it in the expose layer

Replace local same-user logic in the expose security helpers with the shared module.

- remove duplicated ownership comparison logic from `Expose-Security.c`
- keep expose code as a consumer of the common process-access policy

Result:

- no policy drift between expose and other kernel entry points

### 4. Protect the minimum syscall surface

Apply the new helper only to process/task syscalls that target other execution contexts.

Priority set:

- `SysCall_KillProcess`
- `SysCall_GetProcessInfo`
- `SysCall_GetProcessMemoryInfo`
- `SysCall_KillTask` when the task belongs to another process

Rule:

- regular user can target same-user processes only
- admin/kernel can target any process

Result:

- the main cross-process attack surface gets covered without broad churn

### 5. Align shell commands

Apply the same helper to shell commands that inspect or act on foreign processes/tasks.

- process inspection commands
- task inspection commands
- kill commands that target tasks

Result:

- shell behavior matches syscall behavior

### 6. Leave non-goals untouched

Do not expand this lot into unrelated security work.

- no object ACL redesign
- no package policy work
- no window/message ownership policy
- no filesystem permission enforcement

Result:

- small diff, low regression risk

### 7. Document the model

Update `doc/guides/kernel.md` only in the existing security/process sections.

Document:

- effective process owner resolution
- ownership inheritance
- same-user rule
- admin/kernel override
- deny-by-default behavior on missing owner identity

## Verification Plan

### Build

- `bash scripts/linux/build/build --arch x86-64 --fs ext2 --debug`

### Functional checks

- user A can inspect or act on a process owned by user A
- user A cannot inspect or act on a process owned by user B
- admin can inspect or act on both
- child process inherits the expected owner identity

## Why this plan

- minimum changes: touches only ownership resolution and the most relevant process entry points
- clean: one reusable policy helper
- modular: expose, syscalls, and shell reuse the same logic
- evolutive: the helper can later back other ownership-based policies
