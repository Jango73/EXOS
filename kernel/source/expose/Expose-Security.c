
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


    Script Exposure Helpers - Security

\************************************************************************/

#include "Exposed.h"

#include "Kernel.h"
#include "Security.h"
#include "process/Process.h"
#include "process/Schedule.h"
#include "utils/Helpers.h"
#include "UserAccount.h"

/************************************************************************/

/**
 * @brief Retrieves the calling process for exposure access checks.
 * @return Process pointer or NULL when no current process is available.
 */
LPPROCESS ExposeGetCallerProcess(void) {
    return GetCurrentProcess();
}

/************************************************************************/

/**
 * @brief Retrieves the calling user for exposure access checks.
 * @return User account pointer or NULL when no user is logged in.
 */
LPUSERACCOUNT ExposeGetCallerUser(void) {
    return GetCurrentUser();
}

/************************************************************************/

/**
 * @brief Tests whether the calling process runs with kernel privilege.
 * @return TRUE when the calling process has kernel privilege.
 */
BOOL ExposeIsKernelCaller(void) {
    LPPROCESS Caller = ExposeGetCallerProcess();

    SAFE_USE_VALID_ID(Caller, KOID_PROCESS) {
        if (Caller->Privilege != CPU_PRIVILEGE_KERNEL) {
            return FALSE;
        }

        LPUSERACCOUNT User = ExposeGetCallerUser();
        if (User == NULL) {
            return TRUE;
        }

        SAFE_USE_VALID_ID(User, KOID_USERACCOUNT) {
            return User->Privilege == EXOS_PRIVILEGE_ADMIN;
        }
    }

    return FALSE;
}

/************************************************************************/

/**
 * @brief Tests whether the calling user has administrator privilege.
 * @return TRUE when the calling user is an administrator.
 */
BOOL ExposeIsAdminCaller(void) {
    LPUSERACCOUNT User = ExposeGetCallerUser();

    SAFE_USE_VALID_ID(User, KOID_USERACCOUNT) {
        return User->Privilege == EXOS_PRIVILEGE_ADMIN;
    }

    return FALSE;
}

/************************************************************************/

/**
 * @brief Tests whether two processes belong to the same user session.
 * @param Caller Calling process.
 * @param Target Target process.
 * @return TRUE when both processes belong to the same user.
 */
BOOL ExposeIsSameUser(LPPROCESS Caller, LPPROCESS Target) {
    U64 CallerUserIdentifier = U64_Make(0, 0);
    U64 TargetUserIdentifier = U64_Make(0, 0);
    BOOL CallerHasUser = FALSE;
    BOOL TargetHasUser = FALSE;

    SAFE_USE_VALID_ID(Caller, KOID_PROCESS) {
        if (Caller->Session != NULL) {
            LPUSERSESSION Session = Caller->Session;
            SAFE_USE_VALID_ID(Session, KOID_USERSESSION) {
                CallerUserIdentifier = Session->UserID;
                CallerHasUser = TRUE;
            }
        }

        if (CallerHasUser == FALSE) {
            CallerUserIdentifier = Caller->UserID;
            CallerHasUser = TRUE;
        }
    }

    SAFE_USE_VALID_ID(Target, KOID_PROCESS) {
        if (Target->Session != NULL) {
            LPUSERSESSION Session = Target->Session;
            SAFE_USE_VALID_ID(Session, KOID_USERSESSION) {
                TargetUserIdentifier = Session->UserID;
                TargetHasUser = TRUE;
            }
        }

        if (TargetHasUser == FALSE) {
            TargetUserIdentifier = Target->UserID;
            TargetHasUser = TRUE;
        }
    }

    if (CallerHasUser == FALSE || TargetHasUser == FALSE) {
        return FALSE;
    }

    return U64_Cmp(CallerUserIdentifier, TargetUserIdentifier) == 0;
}

/************************************************************************/

/**
 * @brief Tests whether the caller matches the target process.
 * @param Caller Calling process.
 * @param Target Target process.
 * @return TRUE when both pointers refer to the same process.
 */
BOOL ExposeIsOwnerProcess(LPPROCESS Caller, LPPROCESS Target) {
    SAFE_USE_VALID_ID(Caller, KOID_PROCESS) {
        SAFE_USE_VALID_ID(Target, KOID_PROCESS) {
            return Caller == Target;
        }
    }

    return FALSE;
}

/************************************************************************/

/**
 * @brief Determines whether a caller can access a target process.
 * @param Caller Calling process.
 * @param Target Target process (can be NULL when no target is required).
 * @param RequiredAccess Access level flags.
 * @return TRUE when access is granted.
 */
BOOL ExposeCanReadProcess(LPPROCESS Caller, LPPROCESS Target, UINT RequiredAccess) {
    if (RequiredAccess == EXPOSE_ACCESS_PUBLIC) {
        return TRUE;
    }

    if ((RequiredAccess & EXPOSE_ACCESS_KERNEL) != 0u) {
        SAFE_USE_VALID_ID(Caller, KOID_PROCESS) {
            if (Caller->Privilege == CPU_PRIVILEGE_KERNEL) {
                return TRUE;
            }
        }
    }

    if ((RequiredAccess & EXPOSE_ACCESS_ADMIN) != 0u) {
        if (ExposeIsAdminCaller()) {
            return TRUE;
        }
    }

    if ((RequiredAccess & EXPOSE_ACCESS_SAME_USER) != 0u) {
        if (ExposeIsSameUser(Caller, Target)) {
            return TRUE;
        }
    }

    if ((RequiredAccess & EXPOSE_ACCESS_OWNER_PROCESS) != 0u) {
        if (ExposeIsOwnerProcess(Caller, Target)) {
            return TRUE;
        }
    }

    return FALSE;
}

/************************************************************************/
