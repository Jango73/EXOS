
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
 * @brief Initialize the session management system.
 * @return TRUE on success, FALSE on failure.
 */
BOOL InitializeSessionSystem(void) {
    Kernel.UserSessions = NewList(NULL, KernelHeapAlloc, KernelHeapFree);
    if (Kernel.UserSessions == NULL) {
        ERROR(TEXT("Failed to create session list"));
        return FALSE;
    }

    DEBUG(TEXT("Session management system initialized"));
    return TRUE;
}

/************************************************************************/

/**
 * @brief Shutdown the session management system.
 */
void ShutdownSessionSystem(void) {
    SAFE_USE(Kernel.UserSessions) {
        // Clean up all active sessions
        LockMutex(MUTEX_SESSION, INFINITY);

        U32 Count = ListGetSize(Kernel.UserSessions);
        for (U32 i = 0; i < Count; i++) {
            LPUSERSESSION Session = (LPUSERSESSION)ListGetItem(Kernel.UserSessions, i);

            SAFE_USE(Session) {
                U32 UserIdHigh = U64_High32(Session->UserID);
                U32 UserIdLow = U64_Low32(Session->UserID);
                VERBOSE(TEXT("Cleaning up session for user ID: %08X%08X"), UserIdHigh, UserIdLow);
                UNUSED(UserIdHigh);
                UNUSED(UserIdLow);
            }
        }

        DeleteList(Kernel.UserSessions);
        Kernel.UserSessions = NULL;

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

    GetLocalTime(&NewSession->LoginTime);
    NewSession->LastActivity = NewSession->LoginTime;

    // Add to list
    if (ListAddTail(Kernel.UserSessions, NewSession) == 0) {
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
    if (Session == NULL || Session->TypeID != KOID_USERSESSION) {
        return FALSE;
    }

    DATETIME CurrentTime;
    GetLocalTime(&CurrentTime);

    // Check if session has timed out
    U32 CurrentTimeMs =
        CurrentTime.Hour * 3600000 + CurrentTime.Minute * 60000 + CurrentTime.Second * 1000 + CurrentTime.Milli;

    U32 LastActivityMs = Session->LastActivity.Hour * 3600000 + Session->LastActivity.Minute * 60000 +
                         Session->LastActivity.Second * 1000 + Session->LastActivity.Milli;

    // Simple timeout check (simplified for now)
    // TODO: Implement proper time comparison handling day/month/year changes
    if (CurrentTime.Day == Session->LastActivity.Day && CurrentTimeMs > LastActivityMs + SESSION_TIMEOUT_MS) {
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
    ListErase(Kernel.UserSessions, Session);

    UnlockMutex(MUTEX_SESSION);

        U32 UserIdHigh = U64_High32(Session->UserID);
        U32 UserIdLow = U64_Low32(Session->UserID);
        DEBUG(TEXT("Destroyed session for user ID: %08X%08X"), UserIdHigh, UserIdLow);
        UNUSED(UserIdHigh);
        UNUSED(UserIdLow);
}

/************************************************************************/

/**
 * @brief Clean up inactive sessions.
 */
void TimeoutInactiveSessions(void) {
    if (Kernel.UserSessions == NULL) {
        return;
    }

    LockMutex(MUTEX_SESSION, INFINITY);

    U32 Count = ListGetSize(Kernel.UserSessions);
    for (U32 i = 0; i < Count; i++) {
        LPUSERSESSION Session = (LPUSERSESSION)ListGetItem(Kernel.UserSessions, i);
        if (Session != NULL && !ValidateUserSession(Session)) {
            U32 UserIdHigh = U64_High32(Session->UserID);
            U32 UserIdLow = U64_Low32(Session->UserID);
            DEBUG(TEXT("Timing out session for user ID: %08X%08X"), UserIdHigh, UserIdLow);
            UNUSED(UserIdHigh);
            UNUSED(UserIdLow);

            ListErase(Kernel.UserSessions, Session);
            i--;  // Adjust index since list size changed
            Count--;
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
    if (Task == NULL || Kernel.UserSessions == NULL) {
        return NULL;
    }

    LockMutex(MUTEX_SESSION, INFINITY);

    U32 Count = ListGetSize(Kernel.UserSessions);
    for (U32 i = 0; i < Count; i++) {
        LPUSERSESSION Session = (LPUSERSESSION)ListGetItem(Kernel.UserSessions, i);
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
    LPUSERACCOUNT CurrentUser = GetCurrentUser();
    if (CurrentUser == NULL || Kernel.UserSessions == NULL) {
        return NULL;
    }

    // Find the session for the current user
    LockMutex(MUTEX_SESSION, INFINITY);
    U32 Count = ListGetSize(Kernel.UserSessions);
    for (U32 i = 0; i < Count; i++) {
        LPUSERSESSION Session = (LPUSERSESSION)ListGetItem(Kernel.UserSessions, i);
        if (Session != NULL && U64_Cmp(Session->UserID, CurrentUser->UserID) == 0) {
            UnlockMutex(MUTEX_SESSION);
            return Session;
        }
    }
    UnlockMutex(MUTEX_SESSION);
    return NULL;
}

/************************************************************************/

/**
 * @brief Update session activity timestamp.
 * @param Session Session to update.
 */
void UpdateSessionActivity(LPUSERSESSION Session) {
    if (Session == NULL) {
        return;
    }

    GetLocalTime(&Session->LastActivity);

    // CurrentUser is now obtained through GetCurrentUser() via process->session
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
        LPUSERSESSION Found = (LPUSERSESSION)Kernel.UserSessions->First;
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
