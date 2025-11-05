# EXOS Security & User Account System - Implementation Plan

## Current Context

EXOS already has a basic security infrastructure:

- `SECURITY` structure with `Owner` (U64 hash) and per-user permissions
- `Privilege` and `Security` fields in `PROCESS`
- Functional syscall system with 0x70 entry table
- Shell with integrated command parsing
- Mutex and synchronization system

## Existing Strctures

```c
typedef struct tag_SECURITY {
    LISTNODE_FIELDS
    U64 Owner;  // Owner ID (hash)
    U32 UserPermissionCount;
    U32 DefaultPermissions;
    struct {
        U64 UserHash;  // User ID
        U32 Permissions;
    } UserPerms[MAX_SPECIFIC_PERMISSIONS];
} SECURITY, *LPSECURITY;
```

## Proposed Architecture

### Phase 1: User Data Structures

#### 1.1 USERACCOUNT Structure
```c
typedef struct tag_USERACCOUNT {
    LISTNODE_FIELDS
    U64 UserID;                    // Unique user hash
    STR UserName[32];              // Username
    U64 PasswordHash;              // Password hash
    U32 Privilege;                 // Privilege level (0=user, 1=admin)
    DATETIME CreationTime;         // Creation date
    DATETIME LastLoginTime;        // Last login
    U32 Status;                    // Account status (active/suspended)
} USERACCOUNT, *LPUSERACCOUNT;
```

#### 1.2 USERSESSION Structure
```c
typedef struct tag_USERSESSION {
    LISTNODE_FIELDS
    U64 SessionID;                 // Unique session ID
    U64 UserID;                    // Logged in user
    DATETIME LoginTime;            // Login time
    DATETIME LastActivity;         // Last activity
    LPTASK ShellTask;              // Associated shell task
} USERSESSION, *LPUSERSESSION;
```

#### 1.3 PROCESS Extension
```c
// Add to struct tag_PROCESS:
U64 UserID;                       // Owner user
LPUSERSESSION Session;           // User session
```

#### 1.4 User Database
- In-memory hash table for fast access
- Persistent storage in `/system/users.database`
- `root` user created automatically at boot
- Mutex for secure concurrent access

### Phase 2: Authentication

#### 2.1 Hash Functions
```c
U64 HashPassword(LPCSTR Password); -> calls BFEncrypt
BOOL VerifyPassword(LPCSTR Password, U64 StoredHash); -> calls BFDecrypt
U64 GenerateSessionID(void);
```

#### 2.2 Session Management
- In User.c
```c
LPUSERSESSION CreateUserSession(U64 UserID, LPTASK ShellTask);
BOOL ValidateUserSession(LPUSERSESSION Session);
void DestroyUserSession(LPUSERSESSION Session);
void TimeoutInactiveSessions(void);
```

#### 2.3 Global User Context
- In KERNELDATA Kernel
```c
LPLIST (of USERSESSION) UserSessions;
LPUSERACCOUNT CurrentUser;
```

### Phase 3: Syscalls

#### 3.1 New Syscalls (slots 0x30-0x36)
```c
U32 SysCall_Login(U32 Parameter);
U32 SysCall_Logout(U32 Parameter);
U32 SysCall_GetCurrentUser(U32 Parameter);
U32 SysCall_ChangePassword(U32 Parameter);
U32 SysCall_CreateUser(U32 Parameter);
U32 SysCall_DeleteUser(U32 Parameter);
U32 SysCall_ListUsers(U32 Parameter);
```

#### 3.2 Parameter Structures
```c
typedef struct tag_LOGIN_INFO {
    ABI_HEADER;
    STR UserName[MAX_USER_NAME];
    STR Password[MAX_PASSWORD];
} LOGIN_INFO, *LPLOGIN_INFO;

typedef struct tag_PASSWORD_CHANGE {
    ABI_HEADER;
    STR OldPassword[MAX_PASSWORD];
    STR NewPassword[MAX_PASSWORD];
} PASSWORD_CHANGE, *LPPASSWORD_CHANGE;
```

### Phase 4: Shell Commands
- On startup, the shell should do one of two things :
  - if no user account exists, do the add user command
  - if a user account exists, do the login command

#### 4.1 CMD_adduser (name "adduser", alt "newuser")
- Secure prompt for username and password
- Password masking (asterisk display) : a specialized version of ConsolePrint that can take flags
- User account creation

#### 4.2 CMD_deluser (name "deluser", alt "deleteuser")
- Secure prompt for username
- User account deletion

#### 4.3 CMD_login (name "login", alt "login")
- Secure prompt for username and password
- Password masking (asterisk display) : a specialized version of ConsolePrint that can take flags
- Validation and session creation
- Shell prompt update with username

#### 4.4 CMD_logout (name "logout", alt "logout")
- Current session destruction
- Return to login prompt
- Resource cleanup

#### 4.5 CMD_whoami (name "whoami", alt "who")
- Display current user
- Session information (login time, etc.)

#### 4.6 CMD_passwd (name "pass", alt "setpassword")
- Password change
- Old password verification
- New password validation
- Accept blank passwords

### Phase 5: Extended Security Model

#### 5.1 Extended Permissions
```c
#define PERMISSION_NONE         0x00000000
#define PERMISSION_READ         0x00000001
#define PERMISSION_WRITE        0x00000002
#define PERMISSION_EXECUTE      0x00000004
#define PERMISSION_DELETE       0x00000008
#define PERMISSION_ADMIN        0x00000010
#define PERMISSION_CREATE_USER  0x00000020
#define PERMISSION_SYSTEM       0x00000040
```

#### 5.2 File Access Control
- UserID/GroupID verification for file access
- Permissions inherited from parent directories
- Protected system files (kernel, drivers)

#### 5.3 Syscall Restrictions
- Privilege verification before execution
- Administrator syscalls limited to privileged users
- Audit of unauthorized access attempts

#### 5.4 Audit Trail
- Login/logout logging
- Sensitive action recording
- Storage in `/system/audit.log`

## Detailed Implementation

### Files to Create/Modify

#### New Files
- `kernel/source/UserAccount.c` - User account management
- `kernel/source/Session.c` - Session management
- `kernel/include/UserAccount.h` - Structure definitions
- `kernel/include/Session.h` - Session API

#### Files to Modify
- `kernel/source/SYSCall.c` - Add new syscalls
- `kernel/source/Shell.c` - New commands
- `kernel/source/process/Process.c` - PROCESS extension
- `kernel/source/Kernel.c` - User system initialization
- `kernel/include/process/Process.h` - Structure extensions

### Initialization Sequence

1. **Kernel boot**
2. **User database loading** (`/system/users.database`)
3. **Root user creation** if first execution
4. **Shell launch** with login prompt
5. **Login loop** until valid authentication

### Security

#### Protection Measures
- Salted password hashing
- Automatic inactive session timeout
- Brute force attack protection
- Process isolation by user

#### Considerations
- Securely stored passwords
- Tamper-resistant sessions
- Complete system action audit
- Clear user/admin separation

## Migration

### Compatibility
- Current system continues to work without users
- "Legacy" mode for existing processes
- Progressive feature migration

### Testing
- Unit tests for each function
- Complete system integration tests
- Security and attack resistance tests

## Conclusion

This implementation respects the existing EXOS architecture and provides a robust authentication system with modern user management features. The progressive approach allows seamless integration with the current system.
