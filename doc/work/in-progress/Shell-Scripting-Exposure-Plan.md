# Shell Scripting Exposure Refactor Plan

## Goal

Remove direct shell calls to these kernel list getters:

- `GetTaskList`
- `GetProcessMemoryRegionList`
- `GetNetworkDeviceList`
- `GetDriverList`
- `GetDiskList`
- `GetFileSystemList`
- `GetUnusedFileSystemList`
- `GetUsbStorageList`
- `GetUserAccountList`

The shell must stop traversing kernel-owned lists directly. It must consume scripting host objects and arrays only. Missing structures must be exposed first.

## Scope

This refactor targets:

- shell command implementations
- shell script host registration
- missing exposure modules for structures the shell still needs
- one generic shell helper to dump exposed arrays
- one generic script API layer so the shell does not reach into script internals

This refactor does not require changing unrelated shell commands or redesigning the whole shell/kernel boundary in one pass.

## Design Rules

1. The shell must not call the forbidden `Get*List` or `GetProcessMemoryRegionList` functions.
2. The shell must not reimplement list traversal rules that already belong to the scripting exposure layer.
3. Any kernel object discovery done for shell inspection must happen inside exposure code, not in shell code.
4. The shell should read exposed properties through script host descriptors, not by traversing raw kernel lists.
5. Common mechanics must be reusable:
   - script host value access helpers in the script module
   - exposed list dump helpers in the shell module
   - default shell host symbol registration in the exposure module

## Current Violations

### Direct forbidden calls in shell

- `kernel/source/shell/Shell-Commands-System.c`
  - `GetDriverList`
  - `GetTaskList`
  - `GetProcessMemoryRegionList`
  - `GetNetworkDeviceList`
  - `GetUsbStorageList`
- `kernel/source/shell/Shell-Commands-Storage.c`
  - `GetDiskList`
  - `GetFileSystemList`
  - `GetUnusedFileSystemList`
- `kernel/source/shell/Shell-Commands-Users.c`
  - `GetUserAccountList`
- `kernel/source/shell/Shell-Main.c`
  - `GetUserAccountList`
- `kernel/source/shell/Shell-Commands-Core.c`
  - `GetProcessList`
  - `GetDriverList`
  - `GetDiskList`
  - `GetPCIDeviceList`
- `kernel/source/shell/Shell-Commands-Graphics.c`
  - `GetDriverList`

### Structures already exposed

- process
- driver
- storage
- PCI bus / PCI device
- USB ports / USB devices
- keyboard
- mouse
- per-process task arrays

### Structures missing or incomplete for this refactor

- global task array suitable for `kill`
- network devices
- file systems
- unused file systems
- USB storage entries
- user accounts
- kernel memory regions
- centralized shell host symbol registration outside shell code

## Step-by-Step Plan

### Step 1. Add a reusable public script host access API

Goal:

- let shell code consume exposed symbols without including script internals

Work:

- make these helpers publicly available from the script module:
  - `ScriptValueInit`
  - `ScriptValueRelease`
  - `ScriptGetHostSymbolValue`
  - `ScriptGetHostPropertyValue`
  - `ScriptGetHostElementValue`

Responsibilities:

- resolve one registered host symbol by name
- read one property from an exposed object
- read one element from an exposed array
- normalize `SCRIPT_VALUE` ownership using existing script mechanics

Result:

- shell code no longer needs `ScriptFindHostSymbol`
- the access layer is reusable outside shell code

### Step 2. Move default shell host registration into the exposure layer

Goal:

- remove shell-local registration code that directly fetches kernel lists

Work:

- replace `ShellRegisterScriptHostObjects()` with one exposure-owned entry point, for example:
  - `ExposeRegisterDefaultScriptHostObjects(LPSCRIPT_CONTEXT Context)`

Responsibilities:

- register all standard root symbols the shell needs
- keep direct kernel list getter usage out of shell files

Expected registered symbols:

- `process`
- `task`
- `drivers`
- `storage`
- `pci_bus`
- `pci_device`
- `usb`
- `usb_storage`
- `network_device`
- `file_system`
- `unused_file_system`
- `user_account`
- `kernel_memory_region`
- `keyboard`
- `mouse`

Result:

- shell initialization depends on exposure registration only

### Step 3. Add missing exposure modules

Goal:

- expose every structure still needed by shell commands that are using forbidden getters

Work:

- add dedicated exposure files under `kernel/source/expose/`

#### Step 3.1. Add global task exposure

Use case:

- `CMD_killtask`

Requirements:

- add a top-level task array descriptor
- count only tasks visible to the caller
- return only tasks allowed by task/process access policy
- preserve same-user/admin behavior in exposure code

#### Step 3.2. Add network device exposure

Use case:

- `CMD_network`

Expose enough properties to replace `network devices` output:

- device name
- manufacturer
- product
- MAC address
- IPv4 address
- link state
- speed
- duplex
- MTU
- initialized state

#### Step 3.3. Add file system exposure

Use case:

- `CMD_filesystem`

Expose enough properties to replace `fs` output:

- mounted state
- name
- partition scheme/type/format names
- index
- start sector
- sector count
- active flag
- storage and filesystem driver names
- removable / read-only when available

Also expose one root-level property for active partition name if needed.

#### Step 3.4. Add USB storage exposure

Use case:

- `CMD_usb`

Expose enough properties to replace `usb drives` output:

- address
- vendor identifier
- product identifier
- block count
- block size
- present state

#### Step 3.5. Add user account exposure

Use cases:

- first user bootstrap in shell
- `CMD_adduser` first-user detection

Requirements:

- expose a user account array
- make `count` visible enough for bootstrap checks
- restrict element/property access appropriately

#### Step 3.6. Add kernel memory region exposure

Use case:

- `CMD_memorymap`

Expose the kernel memory region list as an array with properties:

- tag
- canonical base
- physical base
- size
- page count
- flags
- attributes
- granularity

Access must remain admin/kernel only.

Result:

- every shell-visible structure needed by this refactor is available through scripting exposure

### Step 4. Add one generic shell helper for exposed arrays

Goal:

- give the shell one standard reusable way to dump exposed lists

Work:

- create a shell helper module, for example:
  - `kernel/include/shell/Shell-Expose.h`
  - `kernel/source/shell/Shell-Expose.c`

Core responsibilities:

- fetch one registered exposed symbol
- read `count`
- iterate elements
- dispatch formatting through a callback

Suggested API shape:

- `ShellExposeGetSymbolValue(...)`
- `ShellExposeGetPropertyValue(...)`
- `ShellExposeGetArrayCount(...)`
- `ShellExposeGetArrayElementValue(...)`
- `ShellExposeDumpArray(...)`
- `ShellExposeDumpSymbolArray(...)`
- `ShellExposeFindArrayElementByStringProperty(...)`

Result:

- no command-specific reimplementation of count / index / property plumbing

### Step 5. Rewrite shell commands to use exposed arrays only

Goal:

- remove all direct shell usage of the forbidden getters

#### Step 5.1. Rewrite `CMD_killtask`

Replace:

- `GetTaskList`
- `ListGetItem`
- direct task pointer traversal in shell

With:

- resolve `task[index]` through the exposure helper
- extract host handle
- pass the handle to `SYSCALL_KillTask`

#### Step 5.2. Rewrite `CMD_memorymap`

Replace:

- `GetProcessMemoryRegionList(&KernelProcess)`
- direct descriptor traversal

With:

- iterate `kernel_memory_region`
- print properties through the shell expose helper

#### Step 5.3. Rewrite `CMD_network`

Replace:

- `GetNetworkDeviceList`
- direct list traversal

With:

- dump `network_device`

#### Step 5.4. Rewrite `CMD_driver`

Replace:

- `GetDriverList`
- direct list traversal for alias search and listing

With:

- iterate `drivers`
- locate one item by exposed `alias`
- print properties through exposure

#### Step 5.5. Rewrite `CMD_disk`

Replace:

- `GetDiskList`

With:

- dump `storage`

#### Step 5.6. Rewrite `CMD_filesystem`

Replace:

- `GetFileSystemList`
- `GetUnusedFileSystemList`

With:

- dump `file_system`
- dump `unused_file_system`
- query exposed active partition info if needed

#### Step 5.7. Rewrite `CMD_usb drives`

Replace:

- `GetUsbStorageList`

With:

- dump `usb_storage`

#### Step 5.8. Rewrite `CMD_adduser`

Replace:

- `GetUserAccountList`

With:

- exposed `user_account.count`

#### Step 5.9. Rewrite shell login bootstrap in `Shell-Main.c`

Replace:

- `GetUserAccountList`

With:

- exposed `user_account.count`

#### Step 5.10. Rewrite shell host registration in `Shell-Commands-Core.c`

Replace:

- shell-local direct list getter registration

With:

- one call into `ExposeRegisterDefaultScriptHostObjects()`

#### Step 5.11. Rewrite the forbidden getter usage in `Shell-Commands-Graphics.c`

Replace:

- `GetDriverList`

With:

- exposed `drivers` lookup by `alias`

This step only removes the forbidden getter usage in that file. It does not redesign the remaining graphics control path.

Result:

- the shell consumes exposed arrays and host handles only for these inspection/control flows

### Step 6. Remove dead code

Goal:

- delete compatibility code left behind by the refactor

Work:

- remove the old shell-local host registration code
- remove dead helpers that only existed to traverse raw kernel lists from shell code
- keep only the generic shell expose helper and exposure-owned registration path

### Step 7. Update documentation

Goal:

- document the architectural boundary, not the low-level command details

Work:

- update `doc/guides/kernel.md`
- keep wording architectural:
  - shell consumes scripting exposure for kernel object inspection
  - exposure owns discovery of shell-visible kernel arrays

### Step 8. Verify

Goal:

- confirm the shell no longer bypasses the exposure layer for these structures

Static verification:

- no shell file calls any forbidden getter anymore
- no shell file traverses these kernel-owned lists directly
- shell uses the shared expose helper for these dumps
- shell host registration is no longer implemented locally with direct getters

Command-level verification:

- `driver list`
- `driver <alias>`
- `disk`
- `fs`
- `fs --long`
- `network devices`
- `usb drives`
- `kill <index>`
- `mem_map`
- first-user bootstrap path
- `add_user` when user list is empty and when it is not

Build verification:

- `git diff --check`
- `bash scripts/linux/build/build --arch x86-64 --fs ext2 --debug --clean`

## Verification Checklist

### Static verification

- no shell file calls any forbidden getter anymore
- no shell file traverses these kernel-owned lists directly
- shell uses the shared expose helper for these dumps
- shell host registration is no longer implemented locally with direct getters

### Command-level verification

- `driver list`
- `driver <alias>`
- `disk`
- `fs`
- `fs --long`
- `network devices`
- `usb drives`
- `kill <index>`
- `mem_map`
- first-user bootstrap path
- `add_user` when user list is empty and when it is not

### Build verification

- `git diff --check`
- `bash scripts/linux/build/build --arch x86-64 --fs ext2 --debug --clean`

## Expected Outcome

After this refactor:

- the shell no longer reaches directly into the forbidden kernel lists
- scripting exposure becomes the single discovery path for these structures in the shell
- missing structures become first-class exposed objects
- shell dumping logic becomes generic and reusable instead of command-specific list traversal
