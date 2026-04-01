# Shell Scripting Exposure Remaining Work

## Goal

Finish the remaining shell refactor work so shell code stops calling the remaining forbidden kernel list getters directly.

The listing-oriented part of the refactor is already done for:

- `driver list`
- `disk`
- `fs`
- `fs --long`
- `network devices`
- `usb ports`
- `usb devices`
- `usb drives`
- `usb device-tree`
- `usb probe`
- `mem_map`

The reusable public script host access API and exposure-owned default host registration are also already in place.

## Remaining Violations

### Direct forbidden calls still present in shell

- `kernel/source/shell/Shell-Commands-System.c`
  - `GetDriverList`
  - `GetTaskList`
- `kernel/source/shell/Shell-Commands-Users.c`
  - `GetUserAccountList`
- `kernel/source/shell/Shell-Main.c`
  - `GetUserAccountList`
- `kernel/source/shell/Shell-Commands-Graphics.c`
  - `GetDriverList`

## Remaining Work

### Step 1. Complete root symbol coverage needed by remaining shell flows

Goal:

- expose the remaining root objects needed by the unfinished shell commands

Work:

- register the remaining standard root symbols through `ExposeRegisterDefaultScriptHostObjects(...)`

Missing roots for the remaining refactor:

- `task`
- `user_account`

Result:

- shell can query tasks and user accounts through the scripting exposure layer only

### Step 2. Add the missing user account exposure

Goal:

- remove direct user list access from shell bootstrap and user creation flow

Use cases:

- first user bootstrap in shell
- `CMD_adduser` first-user detection

Requirements:

- expose a user account array
- make `count` available for bootstrap checks
- restrict element/property access appropriately

Result:

- no shell code calls `GetUserAccountList()` anymore

### Step 3. Rewrite the remaining shell commands

Goal:

- remove the remaining direct shell usage of forbidden getters

#### Step 3.1. Rewrite `CMD_killtask`

Replace:

- `GetTaskList`
- `ListGetItem`
- direct task pointer traversal in shell

With:

- resolve `task[index]` through the public script host access API
- extract the host handle
- pass the resolved task to the existing kill path

#### Step 3.2. Rewrite `CMD_adduser`

Replace:

- `GetUserAccountList`

With:

- exposed `user_account.count`

#### Step 3.3. Rewrite shell login bootstrap in `Shell-Main.c`

Replace:

- `GetUserAccountList`

With:

- exposed `user_account.count`

#### Step 3.4. Rewrite the forbidden getter usage in `Shell-Commands-Graphics.c`

Replace:

- `GetDriverList`

With:

- exposed `drivers` lookup by `alias`

This step only removes the forbidden getter usage in that file. It does not redesign the remaining graphics control path.

Result:

- the shell no longer bypasses the exposure layer for these remaining inspection and control flows

### Step 4. Documentation

Goal:

- document the architectural boundary after the refactor is actually complete

Work:

- update `doc/guides/kernel.md`
- keep wording architectural:
  - shell consumes scripting exposure for kernel object inspection
  - exposure owns discovery of shell-visible kernel arrays

## Verification

### Static verification

- no shell file calls:
  - `GetTaskList`
  - `GetUserAccountList`
  - `GetDriverList`
- shell host registration remains exposure-owned

### Command-level verification

- `kill <index>`
- first-user bootstrap path
- `add_user` when user list is empty and when it is not
- graphics backend alias lookup paths

### Build verification

- `git diff --check`
- `bash scripts/linux/build/build --arch x86-64 --fs ext2 --debug --clean`
