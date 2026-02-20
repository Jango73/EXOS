# EXOS Security & User Account System

## Baseline Security Infrastructure
- [x] `SECURITY` structure with `Owner`, `UserPermissionCount`, `DefaultPermissions`, `UserPerms[]` (`kernel/include/Security.h`).
- [x] `Privilege` and `Security` fields in `PROCESS` (`kernel/include/process/Process.h`).
- [x] Syscall entry table exists and is active (`kernel/source/SYSCallTable.c`, `kernel/source/SYSCall.c`).
- [x] Shell command parser and command table exist (`kernel/source/shell/Shell-Commands.c`).
- [x] Mutex and synchronization system exist (`kernel/include/Mutex.h`, usage in `UserAccount.c` and `UserSession.c`).

## Phase 1 - User Data Structures

### 1.1 USERACCOUNT
- [x] `USERACCOUNT` structure exists with `UserID`, `UserName`, `PasswordHash`, `Privilege`, `CreationTime`, `LastLoginTime`, `Status` (`kernel/include/UserAccount.h`).

### 1.2 USERSESSION
- [x] `USERSESSION` structure exists with `SessionID`, `UserID`, `LoginTime`, `LastActivity`, `ShellTask` (`kernel/include/UserAccount.h`).

### 1.3 PROCESS Extension
- [x] `PROCESS` contains `UserID` and `Session` fields (`kernel/include/process/Process.h`).
- [x] Session inheritance from parent process is implemented (`kernel/source/process/Process.c`).
- [ ] `PROCESS.UserID` is actively maintained by process lifecycle logic.

### 1.4 User Database and Initialization
- [x] In-memory user account container exists (`Kernel.UserAccount` list in `kernel/source/KernelData.c`).
- [x] Persistent user storage is implemented via KernelPath users database (`kernel/source/UserAccount.c`, `kernel/include/utils/KernelPath.h`).
- [x] Access to account/session structures uses mutexes (`MUTEX_ACCOUNTS`, `MUTEX_SESSION`).
- [ ] In-memory hash table for user lookup.
- [ ] Automatic `root` account creation during boot.

## Phase 2 - Authentication

### 2.1 Hash Functions
- [x] `HashPassword`, `VerifyPassword`, `GenerateSessionID` are implemented (`kernel/source/UserAccount.c`).
- [ ] Password hashing based on `BFEncrypt` / verification based on `BFDecrypt`.

### 2.2 Session Management
- [x] `CreateUserSession` implemented (`kernel/source/UserSession.c`).
- [x] `ValidateUserSession` implemented (`kernel/source/UserSession.c`).
- [x] `DestroyUserSession` implemented (`kernel/source/UserSession.c`).
- [x] `TimeoutInactiveSessions` implemented (`kernel/source/UserSession.c`).
- [ ] Automatic periodic execution of inactive-session timeout.

### 2.3 Global User Context
- [x] Global session list exists (`Kernel.UserSessions` in `kernel/source/KernelData.c`).
- [x] Effective current user resolution exists through `process->Session` + `GetCurrentUser()` (`kernel/source/utils/Helpers.c`).
- [ ] Global `CurrentUser` pointer in `KERNELDATA`.

## Phase 3 - Syscalls
- [x] `SysCall_Login` implemented (`kernel/source/SYSCall.c`).
- [x] `SysCall_Logout` implemented (`kernel/source/SYSCall.c`).
- [x] `SysCall_GetCurrentUser` implemented (`kernel/source/SYSCall.c`).
- [x] `SysCall_ChangePassword` implemented (`kernel/source/SYSCall.c`).
- [x] `SysCall_CreateUser` implemented (`kernel/source/SYSCall.c`).
- [x] `SysCall_DeleteUser` implemented (`kernel/source/SYSCall.c`).
- [x] `SysCall_ListUsers` implemented (`kernel/source/SYSCall.c`).
- [x] Syscall table registration exists for all these entries (`kernel/source/SYSCallTable.c`).
- [x] `LOGIN_INFO` and `PASSWORD_CHANGE` structures exist (`kernel/include/User.h`).
- [ ] Authentication syscalls located in slots `0x30-0x36`.

## Phase 4 - Shell Commands

### Startup Login Flow
- [x] Shell checks whether user accounts exist and requests first account creation when empty (`kernel/source/shell/Shell-Main.c`).
- [x] Login loop with bounded retries exists (`kernel/source/shell/Shell-Main.c`).

### Commands
- [x] `add_user` / `new_user` implemented (`CMD_adduser`, `kernel/source/shell/Shell-Commands.c`).
- [x] `del_user` / `delete_user` implemented (`CMD_deluser`, `kernel/source/shell/Shell-Commands.c`).
- [x] `login` implemented (`CMD_login`, `kernel/source/shell/Shell-Commands.c`).
- [x] `logout` implemented (`CMD_logout`, `kernel/source/shell/Shell-Commands.c`).
- [x] `who_am_i` / `who` implemented (`CMD_whoami`, `kernel/source/shell/Shell-Commands.c`).
- [x] `passwd` / `set_password` implemented (`CMD_passwd`, `kernel/source/shell/Shell-Commands.c`).
- [x] Password input masking is implemented through command-line editor hidden input mode (`CommandLineEditorReadLine(..., TRUE)`).
- [ ] Shell prompt includes username after login.

## Phase 5 - Extended Security Model

### 5.1 Extended Permissions
- [x] Base permissions exist (`PERMISSION_NONE`, `PERMISSION_EXECUTE`, `PERMISSION_READ`, `PERMISSION_WRITE`) in `kernel/include/Security.h`.
- [ ] Extended permission set includes `PERMISSION_DELETE`, `PERMISSION_ADMIN`, `PERMISSION_CREATE_USER`, `PERMISSION_SYSTEM`.

### 5.2 File Access Control
- [ ] Enforced file access checks by user identity and per-user permissions.
- [ ] Permission inheritance from parent folders.
- [ ] Protected system file policy enforcement.

### 5.3 Syscall Restrictions
- [x] Global syscall privilege gate is implemented (`SystemCallHandler`, `kernel/source/SYSCall.c`).
- [x] User-management syscalls enforce admin checks (`kernel/source/SYSCall.c`).
- [ ] Audit trail for unauthorized syscall attempts.

### 5.4 Audit Trail
- [ ] Dedicated login/logout audit storage in `/system/audit.log`.
- [ ] Sensitive action audit persistence.

## Detailed File Plan Status

### Planned New Files
- [x] `kernel/source/UserAccount.c`
- [x] `kernel/source/UserSession.c`
- [x] `kernel/include/UserAccount.h`
- [x] `kernel/include/UserSession.h`

### Planned Modified Files
- [x] `kernel/source/SYSCall.c`
- [x] `kernel/source/shell/Shell-Commands.c`
- [x] `kernel/source/process/Process.c`
- [x] `kernel/include/process/Process.h`
- [x] `kernel/source/Kernel.c` (login configuration path)

## Initialization Sequence Status
- [x] Kernel boot phase exists.
- [x] User database loading exists (`LoadUserDatabase`).
- [ ] Automatic `root` user creation on first execution.
- [x] Shell login flow exists (when login feature is enabled).
- [x] Login retry loop exists.

## Security Measures Status
- [x] Salted password hashing exists (salted CRC64).
- [ ] Automatic inactive session timeout execution in runtime scheduler/timer path.
- [ ] Brute-force protection policy beyond basic retry count in shell login loop.
- [ ] Process isolation policy based on user ownership.

## Security Considerations Status
- [x] Passwords are stored as hashes, not plaintext (`PasswordHash`).
- [ ] Tamper-resistant session model.
- [ ] Complete system action audit.
- [x] User/admin privilege separation exists in account and syscall logic.

## Migration and Testing Status
- [x] Login flow can be disabled (`General.DoLogin` configuration).
- [ ] Explicit legacy compatibility mode for pre-user processes.
- [ ] Progressive migration framework for features.
- [ ] Unit tests for each user/security function.
- [ ] Full integration tests for the user/security subsystem.
- [ ] Security/attack-resistance test suite.
