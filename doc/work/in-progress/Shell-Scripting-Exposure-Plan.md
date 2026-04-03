# Shell Scripting Exposure Remaining Work

## Goal

Finish the shell refactor so shell-facing inspection and control paths stop bypassing the scripting exposure layer for user accounts.

## Remaining Violations

### Direct forbidden calls still present in shell

- `kernel/source/shell/Shell-Commands-Users.c`
  - `GetUserAccountList`
- `kernel/source/shell/Shell-Main.c`
  - `GetUserAccountList`

## Remaining Work

### Step 1. Add the missing `user_account` root exposure

Goal:

- expose user accounts through the standard script host registry so shell code can stop reading the kernel user list directly

Required work:

- register `user_account` through `ExposeRegisterDefaultScriptHostObjects(...)`
- add exposure descriptors for:
  - `user_account.count`
  - `user_account[n]`
- expose only the fields actually needed by shell first:
  - enough to answer "is there at least one account?"
- keep access policy explicit and documented

Result:

- shell bootstrap and user creation flow can query account presence through scripting exposure only

### Step 2. Rewrite remaining user bootstrap flows

Goal:

- remove the last shell-side direct calls to `GetUserAccountList()`

#### Step 2.1. Rewrite `CMD_adduser`

Replace:

- `GetUserAccountList`

With:

- exposed `user_account.count`

#### Step 2.2. Rewrite shell login bootstrap in `Shell-Main.c`

Replace:

- `GetUserAccountList`

With:

- exposed `user_account.count`

Result:

- no shell code calls `GetUserAccountList()` anymore

### Step 3. Documentation

Goal:

- document the architectural boundary once the remaining direct user-account getters are gone

Work:

- update `doc/guides/Kernel.md`
- keep wording architectural:
  - shell consumes scripting exposure for shell-visible kernel object discovery
  - exposure owns discovery of shell-visible arrays and roots

## Verification

### Static verification

- no shell file calls:
  - `GetUserAccountList`
- shell host registration remains exposure-owned
- `task`, `driver`, and `graphics` remain registered through expose, not shell-local logic

### Command-level verification

- first-user bootstrap path
- `adduser` when the account list is empty and when it is not

### Build verification

- `git diff --check`
- `bash scripts/linux/build/build --arch x86-32 --fs ext2 --debug --clean`
- `bash scripts/linux/build/build --arch x86-64 --fs ext2 --debug --clean`
