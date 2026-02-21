
/************************************************************************\

    EXOS Kernel
    Copyright (c) 1999-2025 Jango73

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <https://www.gnu.org/licenses/>.


    User Account Management

\************************************************************************/

#ifndef USERACCOUNT_H_INCLUDED
#define USERACCOUNT_H_INCLUDED

/************************************************************************/

#include "Base.h"
#include "Driver.h"
#include "ID.h"
#include "List.h"
#include "Security.h"

// Forward declarations
typedef struct tag_TASK TASK, *LPTASK;

/************************************************************************/

#pragma pack(push, 1)

/************************************************************************/

#define USER_STATUS_ACTIVE 0x00000001
#define USER_STATUS_SUSPENDED 0x00000002
#define USER_STATUS_LOCKED 0x00000004
#define USER_SESSION_LOCK_REASON_TIMEOUT 1
#define USER_SESSION_LOCK_REASON_MANUAL 2

/************************************************************************/

typedef struct tag_USERACCOUNT {
    LISTNODE_FIELDS
    U64 UserID;              // Unique user hash
    STR UserName[32];        // Username
    U64 PasswordHash;        // Password hash
    U32 Privilege;           // Privilege level (0=user, 1=admin)
    DATETIME CreationTime;   // Creation date
    DATETIME LastLoginTime;  // Last login
    U32 Status;              // Account status (active/suspended)
} USERACCOUNT, *LPUSERACCOUNT;

/************************************************************************/

typedef struct tag_USERSESSION {
    LISTNODE_FIELDS
    U64 SessionID;          // Unique session ID
    U64 UserID;             // Logged in user
    DATETIME LoginTime;     // Login time
    DATETIME LastActivity;  // Last activity
    U32 LastActivityMs;     // Last activity uptime in milliseconds
    BOOL IsLocked;          // Session lock state
    U32 LockReason;         // USER_SESSION_LOCK_REASON_*
    DATETIME LockTime;      // Lock time
    U32 FailedUnlockCount;  // Failed unlock attempts
    HANDLE ShellTask;       // Associated shell task (HANDLE to TASK)
} USERSESSION, *LPUSERSESSION;

/************************************************************************/

// Functions in UserAccount.c
BOOL InitializeUserSystem(void);
void ShutdownUserSystem(void);
LPUSERACCOUNT CreateUserAccount(LPCSTR UserName, LPCSTR Password, U32 Privilege);
BOOL DeleteUserAccount(LPCSTR UserName);
LPUSERACCOUNT FindUserAccount(LPCSTR UserName);
LPUSERACCOUNT FindUserAccountByID(U64 UserID);
BOOL ChangeUserPassword(LPCSTR UserName, LPCSTR OldPassword, LPCSTR NewPassword);
BOOL LoadUserDatabase(void);
BOOL SaveUserDatabase(void);

// Hash and authentication functions
U64 HashPassword(LPCSTR Password);
BOOL VerifyPassword(LPCSTR Password, U64 StoredHash);
U64 GenerateSessionID(void);

// Session management functions
LPUSERSESSION CreateUserSession(U64 UserID, HANDLE ShellTask);
BOOL ValidateUserSession(LPUSERSESSION Session);
void DestroyUserSession(LPUSERSESSION Session);
void TimeoutInactiveSessions(void);
LPUSERSESSION GetCurrentSession(void);
BOOL SetCurrentSession(LPUSERSESSION Session);
BOOL IsUserSessionTimedOut(LPUSERSESSION Session);
BOOL IsUserSessionLocked(LPUSERSESSION Session);
BOOL LockUserSession(LPUSERSESSION Session, U32 Reason);
BOOL UnlockUserSession(LPUSERSESSION Session);
BOOL VerifySessionUnlockPassword(LPUSERSESSION Session, LPCSTR Password);
BOOL SessionUserRequiresPassword(LPUSERSESSION Session);

/************************************************************************/

#define USER_SYSTEM_VER_MAJOR 1
#define USER_SYSTEM_VER_MINOR 0

/************************************************************************/

#pragma pack(pop)

#endif  // USERACCOUNT_H_INCLUDED
