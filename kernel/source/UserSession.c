
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


    Session Management

\************************************************************************/

#include "UserSession.h"

#include "Clock.h"
#include "CoreString.h"
#include "Heap.h"
#include "utils/Helpers.h"
#include "Kernel.h"
#include "List.h"
#include "Log.h"
#include "Memory.h"
#include "Mutex.h"
#include "process/Schedule.h"
#include "process/Task.h"
#include "UserAccount.h"

/************************************************************************/

/**
 * @brief Resolve configured inactivity timeout in milliseconds.
 * @return Timeout in milliseconds.
 */
static U32 GetSessionTimeoutMilliseconds(void) {
    LPCSTR TimeoutSecondsText = GetConfigurationValue(TEXT(CONFIG_SESSION_TIMEOUT_SECONDS));
    LPCSTR TimeoutMinutesText = GetConfigurationValue(TEXT(CONFIG_SESSION_TIMEOUT_MINUTES));
    U32 TimeoutMs = SESSION_TIMEOUT_MS;

    if (!STRING_EMPTY(TimeoutSecondsText)) {
        U32 ParsedSeconds = StringToU32(TimeoutSecondsText);
        if (ParsedSeconds > 0) {
            TimeoutMs = ParsedSeconds * 1000;
        }
    } else if (!STRING_EMPTY(TimeoutMinutesText)) {
        U32 ParsedMinutes = StringToU32(TimeoutMinutesText);
        if (ParsedMinutes > 0) {
            TimeoutMs = ParsedMinutes * 60 * 1000;
        }
    }

    return TimeoutMs;
}

/************************************************************************/

/**
 * @brief Test if a user account has a defined non-empty password.
 * @param Account User account to test.
 * @return TRUE when a non-empty password is configured.
 */
static BOOL AccountHasDefinedPassword(LPUSERACCOUNT Account) {
    SAFE_USE(Account) {
        if (VerifyPassword(TEXT(""), Account->PasswordHash)) {
            return FALSE;
        }
        return TRUE;
    }

    return FALSE;
}

/************************************************************************/

/**
 * @brief Initialize the session management system.
 * @return TRUE on success, FALSE on failure.
 */
BOOL InitializeSessionSystem(void) {
    LPLIST SessionList = NewList(NULL, KernelHeapAlloc, KernelHeapFree);
    if (SessionList == NULL) {
        ERROR(TEXT("Failed to create session list"));
        return FALSE;
    }

    SetUserSessionList(SessionList);

    DEBUG(TEXT("Session management system initialized"));
    return TRUE;
}

/************************************************************************/

/**
 * @brief Shutdown the session management system.
 */
void ShutdownSessionSystem(void) {
    LPLIST SessionList = GetUserSessionList();
    SAFE_USE(SessionList) {
        // Clean up all active sessions
        LockMutex(MUTEX_SESSION, INFINITY);

        U32 Count = ListGetSize(SessionList);
        for (U32 i = 0; i < Count; i++) {
            LPUSERSESSION Session = (LPUSERSESSION)ListGetItem(SessionList, i);

            SAFE_USE(Session) {
                U32 UserIdHigh = U64_High32(Session->UserID);
                U32 UserIdLow = U64_Low32(Session->UserID);
                VERBOSE(TEXT("Cleaning up session for user ID: %08X%08X"), UserIdHigh, UserIdLow);
                UNUSED(UserIdHigh);
                UNUSED(UserIdLow);
            }
        }

        DeleteList(SessionList);
        SetUserSessionList(NULL);

        UnlockMutex(MUTEX_SESSION);
    }
}

/************************************************************************/

/**
 * @brief Create a new user session.
 * @param UserID User ID for the session.
 * @param ShellTask Associated shell task.
 * @return Pointer to created session or NULL on failure.
 */
LPUSERSESSION CreateUserSession(U64 UserID, HANDLE ShellTask) {
    LockMutex(MUTEX_SESSION, INFINITY);
    LPLIST SessionList = GetUserSessionList();
    if (SessionList == NULL) {
        UnlockMutex(MUTEX_SESSION);
        return NULL;
    }

    // Allocate new session
    LPUSERSESSION NewSession = (LPUSERSESSION)KernelHeapAlloc(sizeof(USERSESSION));
    if (NewSession == NULL) {
        UnlockMutex(MUTEX_SESSION);
        return NULL;
    }

    // Initialize session
    NewSession->TypeID = KOID_USERSESSION;
    NewSession->References = 1;
    NewSession->Next = NULL;
    NewSession->Prev = NULL;

    NewSession->SessionID = GenerateSessionID();
    NewSession->UserID = UserID;
    NewSession->ShellTask = ShellTask;
    NewSession->IsLocked = FALSE;
    NewSession->LockReason = 0;
    NewSession->FailedUnlockCount = 0;

    GetLocalTime(&NewSession->LoginTime);
    NewSession->LastActivity = NewSession->LoginTime;
    NewSession->LastActivityMs = GetSystemTime();
    NewSession->LockTime = NewSession->LoginTime;

    // Add to list
    if (ListAddTail(SessionList, NewSession) == 0) {
        KernelHeapFree(NewSession);
        UnlockMutex(MUTEX_SESSION);
        return NULL;
    }

    // Update user's last login time
    LPUSERACCOUNT User = FindUserAccountByID(UserID);

    SAFE_USE(User) {
        User->LastLoginTime = NewSession->LoginTime;
    }

    UnlockMutex(MUTEX_SESSION);

    return NewSession;
}

/************************************************************************/

/**
 * @brief Validate a user session.
 * @param Session Session to validate.
 * @return TRUE if session is valid, FALSE otherwise.
 */
BOOL ValidateUserSession(LPUSERSESSION Session) {
    if (Session == NULL || Session->TypeID != KOID_USERSESSION || Session->IsLocked) {
        return FALSE;
    }

    if (IsUserSessionTimedOut(Session)) {
        return FALSE;
    }

    return TRUE;
}

/************************************************************************/

/**
 * @brief Destroy a user session.
 * @param Session Session to destroy.
 */
void DestroyUserSession(LPUSERSESSION Session) {
    if (Session == NULL) {
        return;
    }

    LockMutex(MUTEX_SESSION, INFINITY);

    // Remove from list
    LPLIST SessionList = GetUserSessionList();
    ListErase(SessionList, Session);

    UnlockMutex(MUTEX_SESSION);

    U32 UserIdHigh = U64_High32(Session->UserID);
    U32 UserIdLow = U64_Low32(Session->UserID);
    DEBUG(TEXT("[DestroyUserSession] Destroyed session for user ID: %08X%08X"), UserIdHigh, UserIdLow);
    UNUSED(UserIdHigh);
    UNUSED(UserIdLow);
}

/************************************************************************/

/**
 * @brief Check whether a session inactivity timeout is reached.
 * @param Session Session to test.
 * @return TRUE when inactivity timeout is reached.
 */
BOOL IsUserSessionTimedOut(LPUSERSESSION Session) {
    U32 TimeoutMs;
    U32 CurrentMs;
    U32 ElapsedMs;

    SAFE_USE_VALID_ID(Session, KOID_USERSESSION) {
        TimeoutMs = GetSessionTimeoutMilliseconds();
        CurrentMs = GetSystemTime();
        ElapsedMs = (U32)(CurrentMs - Session->LastActivityMs);
        return (ElapsedMs >= TimeoutMs);
    }

    return FALSE;
}

/************************************************************************/

/**
 * @brief Query session lock state.
 * @param Session Session to inspect.
 * @return TRUE when locked.
 */
BOOL IsUserSessionLocked(LPUSERSESSION Session) {
    SAFE_USE_VALID_ID(Session, KOID_USERSESSION) {
        return Session->IsLocked ? TRUE : FALSE;
    }

    return FALSE;
}

/************************************************************************/

/**
 * @brief Lock one user session.
 * @param Session Session to lock.
 * @param Reason Lock reason code.
 * @return TRUE on success, FALSE on failure.
 */
BOOL LockUserSession(LPUSERSESSION Session, U32 Reason) {
    SAFE_USE_VALID_ID(Session, KOID_USERSESSION) {
        if (Session->IsLocked) {
            return TRUE;
        }

        Session->IsLocked = TRUE;
        Session->LockReason = Reason;
        Session->FailedUnlockCount = 0;
        GetLocalTime(&Session->LockTime);

        return TRUE;
    }

    return FALSE;
}

/************************************************************************/

/**
 * @brief Unlock one user session.
 * @param Session Session to unlock.
 * @return TRUE on success, FALSE on failure.
 */
BOOL UnlockUserSession(LPUSERSESSION Session) {
    SAFE_USE_VALID_ID(Session, KOID_USERSESSION) {
        Session->IsLocked = FALSE;
        Session->LockReason = 0;
        Session->FailedUnlockCount = 0;
        UpdateSessionActivity(Session);
        return TRUE;
    }

    return FALSE;
}

/************************************************************************/

/**
 * @brief Verify one password attempt for unlocking a session.
 * @param Session Locked session.
 * @param Password Password text.
 * @return TRUE when password is valid.
 */
BOOL VerifySessionUnlockPassword(LPUSERSESSION Session, LPCSTR Password) {
    LPUSERACCOUNT Account;

    SAFE_USE_VALID_ID(Session, KOID_USERSESSION) {
        if (Password == NULL) {
            return FALSE;
        }

        Account = FindUserAccountByID(Session->UserID);
        SAFE_USE_VALID_ID(Account, KOID_USERACCOUNT) {
            if (VerifyPassword(Password, Account->PasswordHash)) {
                return TRUE;
            }
        }

        Session->FailedUnlockCount++;
    }

    return FALSE;
}

/************************************************************************/

/**
 * @brief Check whether the session owner has a defined password.
 * @param Session Session to inspect.
 * @return TRUE when lock should require a password.
 */
BOOL SessionUserRequiresPassword(LPUSERSESSION Session) {
    LPUSERACCOUNT Account;

    SAFE_USE_VALID_ID(Session, KOID_USERSESSION) {
        Account = FindUserAccountByID(Session->UserID);
        return AccountHasDefinedPassword(Account);
    }

    return FALSE;
}

/************************************************************************/

/**
 * @brief Lock inactive sessions instead of deleting them.
 */
void TimeoutInactiveSessions(void) {
    LPLIST SessionList = GetUserSessionList();
    if (SessionList == NULL) {
        return;
    }

    LockMutex(MUTEX_SESSION, INFINITY);

    U32 Count = ListGetSize(SessionList);
    for (U32 i = 0; i < Count; i++) {
        LPUSERSESSION Session = (LPUSERSESSION)ListGetItem(SessionList, i);
        if (Session != NULL && Session->IsLocked == FALSE && IsUserSessionTimedOut(Session)) {
            if (!SessionUserRequiresPassword(Session)) {
                UpdateSessionActivity(Session);
                continue;
            }

            U32 UserIdHigh = U64_High32(Session->UserID);
            U32 UserIdLow = U64_Low32(Session->UserID);
            DEBUG(TEXT("[TimeoutInactiveSessions] Locking session for user ID: %08X%08X"), UserIdHigh, UserIdLow);
            UNUSED(UserIdHigh);
            UNUSED(UserIdLow);

            LockUserSession(Session, USER_SESSION_LOCK_REASON_TIMEOUT);
        }
    }

    UnlockMutex(MUTEX_SESSION);
}

/************************************************************************/

/**
 * @brief Find session by associated task.
 * @param Task Task to search for.
 * @return Pointer to session or NULL if not found.
 */
LPUSERSESSION FindSessionByTask(HANDLE Task) {
    LPLIST SessionList = GetUserSessionList();
    if (Task == NULL || SessionList == NULL) {
        return NULL;
    }

    LockMutex(MUTEX_SESSION, INFINITY);

    U32 Count = ListGetSize(SessionList);
    for (U32 i = 0; i < Count; i++) {
        LPUSERSESSION Session = (LPUSERSESSION)ListGetItem(SessionList, i);
        if (Session != NULL && Session->ShellTask == Task) {
            UnlockMutex(MUTEX_SESSION);
            return Session;
        }
    }

    UnlockMutex(MUTEX_SESSION);
    return NULL;
}

/************************************************************************/

/**
 * @brief Get the current active session.
 * @return Pointer to current session or NULL if none.
 */
LPUSERSESSION GetCurrentSession(void) {
    LPPROCESS CurrentProcess = GetCurrentProcess();
    if (CurrentProcess == NULL) {
        return NULL;
    }

    SAFE_USE_VALID_ID(CurrentProcess->Session, KOID_USERSESSION) {
        return CurrentProcess->Session;
    }

    return NULL;
}

/************************************************************************/

/**
 * @brief Update session activity timestamp.
 * @param Session Session to update.
 */
void UpdateSessionActivity(LPUSERSESSION Session) {
    SAFE_USE_VALID_ID(Session, KOID_USERSESSION) {
        if (Session->IsLocked) {
            return;
        }

        GetLocalTime(&Session->LastActivity);
        Session->LastActivityMs = GetSystemTime();
    }
}

/************************************************************************/

/**
 * @brief Set the current user session.
 * @param Session The session to set as current.
 * @return TRUE on success, FALSE on failure.
 */
BOOL SetCurrentSession(LPUSERSESSION Session) {
    LPPROCESS CurrentProcess = GetCurrentProcess();
    if (CurrentProcess == NULL) {
        return FALSE;
    }

    // Find the session in the session list first
    SAFE_USE(Session) {
        LPLIST SessionList = GetUserSessionList();
        LPUSERSESSION Found = (LPUSERSESSION)(SessionList != NULL ? SessionList->First : NULL);
        BOOL SessionExists = FALSE;

        while (Found != NULL) {
            if (Found == Session) {
                SessionExists = TRUE;
                break;
            }
            Found = (LPUSERSESSION)Found->Next;
        }

        if (!SessionExists) {
            return FALSE;
        }
    }

    // Associate the session with the current process
    CurrentProcess->Session = Session;

    return TRUE;
}
