
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

#include "UserAccount.h"

#include "Clock.h"
#include "utils/Crypt.h"
#include "utils/Database.h"
#include "Heap.h"
#include "utils/Helpers.h"
#include "Kernel.h"
#include "List.h"
#include "Log.h"
#include "Memory.h"
#include "Mutex.h"
#include "CoreString.h"
#include "System.h"

/************************************************************************/

static U32 NextSessionID = 1;
static const U32 USER_DATABASE_CAPACITY = 1000;

/************************************************************************/

/**
 * @brief Initialize the user account system.
 * @return TRUE on success, FALSE on failure.
 */
BOOL InitializeUserSystem(void) {
    if (Kernel.UserAccount == NULL) {
        ERROR(TEXT("User account list not initialized in kernel"));
        return FALSE;
    }

    // Try to load existing user database
    if (!LoadUserDatabase()) {
        DEBUG(TEXT("No existing user database found - will let shell handle user creation"));
    }

    InitializeSessionSystem();

    DEBUG(TEXT("User account system initialized"));
    return TRUE;
}

/************************************************************************/

/**
 * @brief Shutdown the user account system.
 */
void ShutdownUserSystem(void) {
    SAFE_USE(Kernel.UserAccount) {
        SaveUserDatabase();
        ListReset(Kernel.UserAccount);
    }
}

/************************************************************************/

/**
 * @brief Create a new user account.
 * @param UserName Username for the account.
 * @param Password Plain text password.
 * @param Privilege Privilege level (EXOS_PRIVILEGE_USER or EXOS_PRIVILEGE_ADMIN).
 * @return Pointer to created user account or NULL on failure.
 */
LPUSERACCOUNT CreateUserAccount(LPCSTR UserName, LPCSTR Password, U32 Privilege) {
    DEBUG(TEXT("[CreateUserAccount] Enter - UserName=%s"), UserName ? UserName : TEXT("NULL"));

    if (UserName == NULL || Password == NULL) {
        DEBUG(TEXT("[CreateUserAccount] NULL parameters - UserName=%p, Password=%p"), UserName, Password);
        return NULL;
    }

    U32 UserNameLen = StringLength(UserName);
    if (UserNameLen == 0 || UserNameLen >= 32) {
        DEBUG(TEXT("[CreateUserAccount] Invalid username length: %d"), UserNameLen);
        return NULL;
    }

    DEBUG(TEXT("[CreateUserAccount] Attempting to lock mutex"));
    LockMutex(MUTEX_ACCOUNTS, INFINITY);

    // Check if user already exists
    DEBUG(TEXT("[CreateUserAccount] Checking if user exists"));

    if (FindUserAccount(UserName) != NULL) {
        DEBUG(TEXT("[CreateUserAccount] User already exists"));
        UnlockMutex(MUTEX_ACCOUNTS);
        return NULL;
    }

    // Allocate new user account
    DEBUG(TEXT("[CreateUserAccount] Allocating memory for new user"));
    LPUSERACCOUNT NewUser = (LPUSERACCOUNT)KernelHeapAlloc(sizeof(USERACCOUNT));
    if (NewUser == NULL) {
        DEBUG(TEXT("[CreateUserAccount] Memory allocation failed"));
        UnlockMutex(MUTEX_ACCOUNTS);
        return NULL;
    }

    // Initialize user account
    MemorySet(NewUser, 0, sizeof(USERACCOUNT));
    NewUser->TypeID = KOID_USERACCOUNT;
    NewUser->References = 1;

    StringCopy(NewUser->UserName, UserName);
    NewUser->UserID = HashString(UserName);
    NewUser->PasswordHash = HashPassword(Password);
    NewUser->Privilege = Privilege;
    NewUser->Status = USER_STATUS_ACTIVE;

    GetLocalTime(&NewUser->CreationTime);
    NewUser->LastLoginTime = NewUser->CreationTime;

    // Add to list and database
    DEBUG(TEXT("[CreateUserAccount] Adding to user list"));
    if (ListAddTail(Kernel.UserAccount, NewUser) == 0) {
        DEBUG(TEXT("[CreateUserAccount] Failed to add to user list"));
        KernelHeapFree(NewUser);
        UnlockMutex(MUTEX_ACCOUNTS);
        return NULL;
    }

    UnlockMutex(MUTEX_ACCOUNTS);

    if (!SaveUserDatabase()) {
        ERROR(TEXT("[CreateUserAccount] Failed to save user database after creating user %s"), UserName);
    }

    DEBUG(TEXT("[CreateUserAccount] User created successfully"));
    VERBOSE(TEXT("Created user account: %s"), UserName);
    return NewUser;
}

/************************************************************************/

/**
 * @brief Delete a user account.
 * @param UserName Username to delete.
 * @return TRUE on success, FALSE on failure.
 */
BOOL DeleteUserAccount(LPCSTR UserName) {
    if (UserName == NULL) {
        return FALSE;
    }

    // Don't allow deleting root user
    if (StringCompare(UserName, TEXT("root")) == 0) {
        return FALSE;
    }

    LockMutex(MUTEX_ACCOUNTS, INFINITY);

    LPUSERACCOUNT User = FindUserAccount(UserName);
    if (User == NULL) {
        UnlockMutex(MUTEX_ACCOUNTS);
        return FALSE;
    }

    ListErase(Kernel.UserAccount, User);

    UnlockMutex(MUTEX_ACCOUNTS);

    VERBOSE(TEXT("Deleted user account: %s"), UserName);
    return TRUE;
}

/************************************************************************/

/**
 * @brief Find a user account by username.
 * @param UserName Username to search for.
 * @return Pointer to user account or NULL if not found.
 */
LPUSERACCOUNT FindUserAccount(LPCSTR UserName) {
    if (UserName == NULL || Kernel.UserAccount == NULL) {
        return NULL;
    }

    U32 Count = ListGetSize(Kernel.UserAccount);
    for (U32 i = 0; i < Count; i++) {
        LPUSERACCOUNT User = (LPUSERACCOUNT)ListGetItem(Kernel.UserAccount, i);
        if (User != NULL && STRINGS_EQUAL(User->UserName, UserName)) {
            return User;
        }
    }

    return NULL;
}

/************************************************************************/

/**
 * @brief Find a user account by user ID.
 * @param UserID User ID hash to search for.
 * @return Pointer to user account or NULL if not found.
 */
LPUSERACCOUNT FindUserAccountByID(U64 UserID) {
    if (Kernel.UserAccount == NULL) {
        return NULL;
    }

    U32 Count = ListGetSize(Kernel.UserAccount);
    for (U32 i = 0; i < Count; i++) {
        LPUSERACCOUNT User = (LPUSERACCOUNT)ListGetItem(Kernel.UserAccount, i);
        if (User != NULL && U64_Cmp(User->UserID, UserID) == 0) {
            return User;
        }
    }

    return NULL;
}

/************************************************************************/

/**
 * @brief Change a user's password.
 * @param UserName Username.
 * @param OldPassword Current password.
 * @param NewPassword New password.
 * @return TRUE on success, FALSE on failure.
 */
BOOL ChangeUserPassword(LPCSTR UserName, LPCSTR OldPassword, LPCSTR NewPassword) {
    if (UserName == NULL || OldPassword == NULL || NewPassword == NULL) {
        return FALSE;
    }

    LockMutex(MUTEX_ACCOUNTS, INFINITY);

    LPUSERACCOUNT User = FindUserAccount(UserName);
    if (User == NULL) {
        UnlockMutex(MUTEX_ACCOUNTS);
        return FALSE;
    }

    // Verify old password
    if (!VerifyPassword(OldPassword, User->PasswordHash)) {
        UnlockMutex(MUTEX_ACCOUNTS);
        return FALSE;
    }

    // Set new password
    User->PasswordHash = HashPassword(NewPassword);

    UnlockMutex(MUTEX_ACCOUNTS);

    VERBOSE(TEXT("Password changed for user: %s"), UserName);
    return TRUE;
}

/************************************************************************/

/**
 * @brief Load user database from persistent storage.
 * @return TRUE on success, FALSE on failure.
 */
BOOL LoadUserDatabase(void) {
    DATABASE* Database = DatabaseCreate(sizeof(USERACCOUNT), (U32)((U8*)&((USERACCOUNT*)0)->UserID - (U8*)0), USER_DATABASE_CAPACITY);
    if (Database == NULL) {
        ERROR(TEXT("Failed to allocate temporary user database"));
        return FALSE;
    }

    I32 Result = DatabaseLoad(Database, TEXT(PATH_USERS_DATABASE));
    if (Result != 0) {
        DatabaseFree(Database);
        return FALSE;
    }

    LockMutex(MUTEX_ACCOUNTS, INFINITY);

    ListReset(Kernel.UserAccount);

    for (U32 i = 0; i < Database->Count; i++) {
        LPUSERACCOUNT User = (LPUSERACCOUNT)((U8*)Database->Records + i * Database->RecordSize);
        LPUSERACCOUNT NewUser = (LPUSERACCOUNT)KernelHeapAlloc(sizeof(USERACCOUNT));

        SAFE_USE(NewUser) {
            MemoryCopy(NewUser, User, sizeof(USERACCOUNT));
            NewUser->Next = NULL;
            NewUser->Prev = NULL;
            NewUser->References = 1;
            NewUser->TypeID = KOID_USERACCOUNT;

            if (ListAddTail(Kernel.UserAccount, NewUser) == 0) {
                KernelHeapFree(NewUser);
            }
        }
    }

    UnlockMutex(MUTEX_ACCOUNTS);

    DEBUG(TEXT("Loaded %u user accounts from database"), Database->Count);

    DatabaseFree(Database);
    return TRUE;
}

/************************************************************************/

/**
 * @brief Save user database to persistent storage.
 * @return TRUE on success, FALSE on failure.
 */
BOOL SaveUserDatabase(void) {
    DATABASE* Database = DatabaseCreate(sizeof(USERACCOUNT), (U32)((U8*)&((USERACCOUNT*)0)->UserID - (U8*)0), USER_DATABASE_CAPACITY);
    if (Database == NULL) {
        ERROR(TEXT("Failed to allocate temporary user database"));
        return FALSE;
    }

    LockMutex(MUTEX_ACCOUNTS, INFINITY);

    SAFE_USE(Kernel.UserAccount) {
        U32 Count = ListGetSize(Kernel.UserAccount);
        for (U32 i = 0; i < Count && Database->Count < Database->Capacity; i++) {
            LPUSERACCOUNT User = (LPUSERACCOUNT)ListGetItem(Kernel.UserAccount, i);

            SAFE_USE(User) {
                DatabaseAdd(Database, User);
            }
        }
    }

    UnlockMutex(MUTEX_ACCOUNTS);

    U32 SavedCount = Database->Count;
    I32 Result = DatabaseSave(Database, TEXT(PATH_USERS_DATABASE));

    DatabaseFree(Database);

    if (Result != 0) {
        return FALSE;
    }

    return TRUE;
}

/************************************************************************/

/**
 * @brief Hash a password using CRC64.
 * @param Password Plain text password.
 * @return 64-bit hash of the password.
 */
U64 HashPassword(LPCSTR Password) {
    if (Password == NULL) {
        return U64_FromU32(0);
    }

    // Add salt to password
    STR SaltedPassword[128];
    StringCopy(SaltedPassword, TEXT("EXOS_SALT_"));
    StringConcat(SaltedPassword, Password);
    StringConcat(SaltedPassword, TEXT("_TLAS_SOXE"));

    return CRC64_Hash(SaltedPassword, StringLength(SaltedPassword));
}

/************************************************************************/

/**
 * @brief Verify a password against a stored hash.
 * @param Password Plain text password to verify.
 * @param StoredHash Stored password hash.
 * @return TRUE if password matches, FALSE otherwise.
 */
BOOL VerifyPassword(LPCSTR Password, U64 StoredHash) {
    if (Password == NULL) {
        return FALSE;
    }

    U64 PasswordHash = HashPassword(Password);
    return U64_Cmp(PasswordHash, StoredHash) == 0;
}

/************************************************************************/

/**
 * @brief Generate a unique session ID.
 * @return New session ID.
 */
U64 GenerateSessionID(void) {
    U64 SessionID = U64_FromU32(NextSessionID);
    NextSessionID++;

    // Add some entropy based on system time
    DATETIME CurrentTime;
    GetLocalTime(&CurrentTime);
    U64 TimeHash = U64_FromU32(
        CurrentTime.Year ^ CurrentTime.Month ^ CurrentTime.Day ^ CurrentTime.Hour ^ CurrentTime.Minute ^
        CurrentTime.Second);

    return U64_Add(SessionID, TimeHash);
}

