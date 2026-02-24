
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


    Shell commands

\************************************************************************/

#include "shell/Shell-Commands-Private.h"

U32 CMD_adduser(LPSHELLCONTEXT Context) {
    STR UserName[MAX_USER_NAME];
    STR Password[MAX_USER_NAME];
    STR PrivilegeStr[16];
    U32 Privilege = EXOS_PRIVILEGE_ADMIN;  // Default to admin for first user


    ParseNextCommandLineComponent(Context);
    if (StringLength(Context->Command) > 0) {
        StringCopy(UserName, Context->Command);
    } else {
        ConsolePrint(TEXT("Enter username: "));
        ConsoleGetString(UserName, MAX_USER_NAME - 1);
        if (StringLength(UserName) == 0) {
            ConsolePrint(TEXT("ERROR: Username cannot be empty\n"));
            return DF_RETURN_SUCCESS;
        }
    }

    ConsolePrint(TEXT("Password: "));
    CommandLineEditorReadLine(&Context->Input.Editor, Context->Input.CommandLine, sizeof Context->Input.CommandLine, TRUE);
    StringCopy(Password, Context->Input.CommandLine);


    // Check if this is the first user (no users exist yet)
    LPLIST UserAccountList = GetUserAccountList();
    BOOL IsFirstUser = (UserAccountList == NULL || UserAccountList->First == NULL);
    if (IsFirstUser) {
        Privilege = EXOS_PRIVILEGE_ADMIN;
    } else {
        ConsolePrint(TEXT("Admin user? (y/n): "));
        ConsoleGetString(PrivilegeStr, 15);

        if (StringCompareNC(PrivilegeStr, TEXT("y")) == 0 || StringCompareNC(PrivilegeStr, TEXT("yes")) == 0) {
            Privilege = EXOS_PRIVILEGE_ADMIN;
        } else {
            Privilege = EXOS_PRIVILEGE_USER;
        }
    }


    LPUSERACCOUNT Account = CreateUserAccount(UserName, Password, Privilege);

    SAFE_USE(Account) {
    } else {
        ConsolePrint(TEXT("ERROR: Failed to create user '%s'\n"), UserName);
    }


    return DF_RETURN_SUCCESS;
}

/***************************************************************************/

U32 CMD_deluser(LPSHELLCONTEXT Context) {
    STR UserName[MAX_USER_NAME];

    ParseNextCommandLineComponent(Context);
    if (StringLength(Context->Command) > 0) {
        StringCopy(UserName, Context->Command);
    } else {
        ConsolePrint(TEXT("Username to delete: "));
        ConsoleGetString(UserName, MAX_USER_NAME - 1);
        if (StringLength(UserName) == 0) {
            ConsolePrint(TEXT("Username cannot be empty\n"));
            return DF_RETURN_SUCCESS;
        }
    }

    LPUSERSESSION Session = GetCurrentSession();

    SAFE_USE(Session) {
        LPUSERACCOUNT CurrentAccount = FindUserAccountByID(Session->UserID);

        if (CurrentAccount == NULL || CurrentAccount->Privilege != EXOS_PRIVILEGE_ADMIN) {
            ConsolePrint(TEXT("Only admin users can delete accounts\n"));
            return DF_RETURN_SUCCESS;
        }
    }

    if (DeleteUserAccount(UserName)) {
        ConsolePrint(TEXT("User '%s' deleted successfully\n"), UserName);
        SaveUserDatabase();
    } else {
        ConsolePrint(TEXT("Failed to delete user '%s'\n"), UserName);
    }

    return DF_RETURN_SUCCESS;
}

/***************************************************************************/

U32 CMD_login(LPSHELLCONTEXT Context) {
    STR UserName[MAX_USER_NAME];
    STR Password[MAX_USER_NAME];


    ParseNextCommandLineComponent(Context);

    if (StringLength(Context->Command) > 0) {
        StringCopy(UserName, Context->Command);
    } else {
        ConsolePrint(TEXT("Username: "));
        ConsoleGetString(UserName, MAX_USER_NAME - 1);


        if (StringLength(UserName) == 0) {
            ConsolePrint(TEXT("ERROR: Username cannot be empty\n"));
            return DF_RETURN_SUCCESS;
        }
    }

    ConsolePrint(TEXT("Password: "));
    CommandLineEditorReadLine(&Context->Input.Editor, Context->Input.CommandLine, sizeof Context->Input.CommandLine, TRUE);
    StringCopy(Password, Context->Input.CommandLine);

    LPUSERACCOUNT Account = FindUserAccount(UserName);
    if (Account == NULL) {
        ConsolePrint(TEXT("ERROR: User '%s' not found\n"), UserName);
        return DF_RETURN_SUCCESS;
    }

    if (!VerifyPassword(Password, Account->PasswordHash)) {
        ConsolePrint(TEXT("ERROR: Invalid password\n"));
        return DF_RETURN_SUCCESS;
    }

    LPUSERSESSION Session = CreateUserSession(Account->UserID, (HANDLE)GetCurrentTask());
    if (Session == NULL) {
        ConsolePrint(TEXT("ERROR: Failed to create session\n"));
        return DF_RETURN_SUCCESS;
    }

    GetLocalTime(&Account->LastLoginTime);

    if (SetCurrentSession(Session)) {
    } else {
        ConsolePrint(TEXT("ERROR: Failed to set session\n"));
        DestroyUserSession(Session);
    }


    return DF_RETURN_SUCCESS;
}

/***************************************************************************/

U32 CMD_logout(LPSHELLCONTEXT Context) {
    UNUSED(Context);

    LPUSERSESSION Session = GetCurrentSession();
    if (Session == NULL) {
        ConsolePrint(TEXT("No active session\n"));
        return DF_RETURN_SUCCESS;
    }

    DestroyUserSession(Session);
    SetCurrentSession(NULL);
    ConsolePrint(TEXT("Logged out successfully\n"));

    return DF_RETURN_SUCCESS;
}

/***************************************************************************/

U32 CMD_whoami(LPSHELLCONTEXT Context) {
    UNUSED(Context);

    LPUSERSESSION Session = GetCurrentSession();
    if (Session == NULL) {
        ConsolePrint(TEXT("No active session\n"));
        return DF_RETURN_SUCCESS;
    }

    LPUSERACCOUNT Account = FindUserAccountByID(Session->UserID);
    if (Account == NULL) {
        ConsolePrint(TEXT("Session user not found\n"));
        return DF_RETURN_SUCCESS;
    }

    ConsolePrint(TEXT("Current user: %s\n"), Account->UserName);
    ConsolePrint(TEXT("Privilege: %s\n"), Account->Privilege == EXOS_PRIVILEGE_ADMIN ? TEXT("Admin") : TEXT("User"));
    ConsolePrint(
        TEXT("Login time: %d/%d/%d %d:%d:%d\n"), Session->LoginTime.Day, Session->LoginTime.Month,
        Session->LoginTime.Year, Session->LoginTime.Hour, Session->LoginTime.Minute, Session->LoginTime.Second);
    ConsolePrint(TEXT("Session ID: %lld\n"), Session->SessionID);

    return DF_RETURN_SUCCESS;
}

/***************************************************************************/

U32 CMD_passwd(LPSHELLCONTEXT Context) {
    UNUSED(Context);
    STR OldPassword[MAX_PASSWORD];
    STR NewPassword[MAX_PASSWORD];
    STR ConfirmPassword[MAX_PASSWORD];

    LPUSERSESSION Session = GetCurrentSession();
    if (Session == NULL) {
        ConsolePrint(TEXT("No active session\n"));
        return DF_RETURN_SUCCESS;
    }

    LPUSERACCOUNT Account = FindUserAccountByID(Session->UserID);
    if (Account == NULL) {
        ConsolePrint(TEXT("Session user not found\n"));
        return DF_RETURN_SUCCESS;
    }

    ConsolePrint(TEXT("Password: "));
    CommandLineEditorReadLine(&Context->Input.Editor, Context->Input.CommandLine, sizeof Context->Input.CommandLine, TRUE);
    StringCopy(OldPassword, Context->Input.CommandLine);

    if (!VerifyPassword(OldPassword, Account->PasswordHash)) {
        ConsolePrint(TEXT("Invalid current password\n"));
        return DF_RETURN_SUCCESS;
    }

    ConsolePrint(TEXT("New password: "));
    CommandLineEditorReadLine(&Context->Input.Editor, Context->Input.CommandLine, sizeof Context->Input.CommandLine, TRUE);
    StringCopy(NewPassword, Context->Input.CommandLine);

    ConsolePrint(TEXT("Confirm password: "));
    CommandLineEditorReadLine(&Context->Input.Editor, Context->Input.CommandLine, sizeof Context->Input.CommandLine, TRUE);
    StringCopy(ConfirmPassword, Context->Input.CommandLine);

    if (StringCompare(NewPassword, ConfirmPassword) != 0) {
        ConsolePrint(TEXT("Passwords do not match\n"));
        return DF_RETURN_SUCCESS;
    }

    if (ChangeUserPassword(Account->UserName, OldPassword, NewPassword)) {
        ConsolePrint(TEXT("Password changed successfully\n"));
        SaveUserDatabase();
    } else {
        ConsolePrint(TEXT("Failed to change password\n"));
    }

    return DF_RETURN_SUCCESS;
}

/***************************************************************************/

