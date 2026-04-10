# Shell Scripting Exposure Remaining Work

## Goal

Finish the shell refactor so shell-facing inspection and control paths stop bypassing the scripting exposure layer for account-related shell features.

## Remaining Violations

### Direct forbidden calls still present in shell

- `kernel/source/shell/Shell-Commands-System.c`
  - `KernelEnumGetProvider`
  - `KernelEnumNext`

### Already addressed in this branch

- the shell account bootstrap path uses `account.count` through the exposure layer instead of `GetUserAccountList()`
- `adduser`, `deluser`, and `passwd` route account management through embedded E0 scripts and syscalls instead of direct kernel account functions
- the exposure root name is `account`

## Remaining Work

### Step 1. Add the missing `account` root exposure

Goal:

- expose user accounts through the standard script host registry so shell code can stop reading the kernel user list directly

Required work:

- register `account` through `ExposeRegisterDefaultScriptHostObjects(...)`
- add exposure descriptors for:
  - `account.count`
  - `account[n]`
- expose only the fields actually needed by shell first:
  - enough to answer "is there at least one account?"
- keep access policy explicit and documented

Result:

- shell bootstrap and user creation flow can query account presence through scripting exposure only

### Step 2. Keep account management script-owned

Goal:

- remove the last shell-side direct calls to `GetUserAccountList()`

How:

- keep account creation / deletion / password change reachable through script-call helpers only
- keep shell commands limited to prompting, argument preparation, and embedded script execution
- avoid new direct shell calls to account ownership functions

### Step 3.

Goal:

- remove the last shell-side direct calls to `KernelEnumGetProvider()`
- remove the last shell-side direct calls to `KernelEnumNext()`

How:

- create an embedded script for the nvme listing, like in CMD_usb

## Verification

### Static verification

- no shell file calls:
  - `GetUserAccountList`
  - `KernelEnumGetProvider`
  - `KernelEnumNext`
- shell host registration remains exposure-owned
- `task`, `driver`, and `graphics` remain registered through expose, not shell-local logic

### Command-level verification

- first-user bootstrap path
- `adduser` when the account list is empty and when it is not
- `deluser`
- `passwd`

### Build verification

- `git diff --check`
- `bash scripts/linux/build/build --arch x86-32 --fs ext2 --debug --clean`
- `bash scripts/linux/build/build --arch x86-64 --fs ext2 --debug --clean`
