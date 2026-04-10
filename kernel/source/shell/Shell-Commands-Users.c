
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
#include "shell/Shell-EmbeddedScripts.h"

/************************************************************************/

/**
 * @brief Run the embedded account creation script with prepared variables.
 * @param Context Shell context.
 * @param UserName Target user name.
 * @param Password Target password.
 * @param Privilege Target privilege.
 * @return `DF_RETURN_*` status code.
 */
static UINT RunEmbeddedAccountCreateScript(
    LPSHELLCONTEXT Context,
    LPCSTR UserName,
    LPCSTR Password,
    U32 Privilege) {
    STR ScriptText[4096];

    if (Context == NULL || UserName == NULL || Password == NULL) {
        return DF_RETURN_BAD_PARAMETER;
    }

    StringPrintFormat(
        ScriptText,
        TEXT("target_user_name = \"%s\";\n"
             "target_password = \"%s\";\n"
             "target_privilege = %u;\n"
             "%s"),
        UserName,
        Password,
        Privilege,
        ShellGetEmbeddedScript(SHELL_EMBEDDED_SCRIPT_ACCOUNT_CREATE));
    return RunEmbeddedScript(Context, ScriptText);
}

/************************************************************************/

/**
 * @brief Run the embedded account deletion script with prepared variables.
 * @param Context Shell context.
 * @param UserName Target user name.
 * @return `DF_RETURN_*` status code.
 */
static UINT RunEmbeddedAccountDeleteScript(
    LPSHELLCONTEXT Context,
    LPCSTR UserName) {
    STR ScriptText[2048];

    if (Context == NULL || UserName == NULL) {
        return DF_RETURN_BAD_PARAMETER;
    }

    StringPrintFormat(
        ScriptText,
        TEXT("target_user_name = \"%s\";\n%s"),
        UserName,
        ShellGetEmbeddedScript(SHELL_EMBEDDED_SCRIPT_ACCOUNT_DELETE));
    return RunEmbeddedScript(Context, ScriptText);
}

/************************************************************************/

/**
 * @brief Run the embedded account password change script with prepared variables.
 * @param Context Shell context.
 * @param OldPassword Current password.
 * @param NewPassword New password.
 * @return `DF_RETURN_*` status code.
 */
static UINT RunEmbeddedAccountChangePasswordScript(
    LPSHELLCONTEXT Context,
    LPCSTR OldPassword,
    LPCSTR NewPassword) {
    STR ScriptText[4096];

    if (Context == NULL || OldPassword == NULL || NewPassword == NULL) {
        return DF_RETURN_BAD_PARAMETER;
    }

    StringPrintFormat(
        ScriptText,
        TEXT("target_old_password = \"%s\";\n"
             "target_new_password = \"%s\";\n"
             "%s"),
        OldPassword,
        NewPassword,
        ShellGetEmbeddedScript(SHELL_EMBEDDED_SCRIPT_ACCOUNT_CHANGE_PASSWORD));
    return RunEmbeddedScript(Context, ScriptText);
}

/************************************************************************/

/**
 * @brief Clear one handled script error from the shell script context.
 * @param Context Shell context.
 */
static void ShellClearHandledScriptError(LPSHELLCONTEXT Context) {
    if (Context == NULL || Context->ScriptContext == NULL) {
        return;
    }

    Context->ScriptContext->ErrorCode = SCRIPT_OK;
    Context->ScriptContext->ErrorMessage[0] = STR_NULL;
}

/************************************************************************/

U32 CMD_adduser(LPSHELLCONTEXT Context) {
    STR UserName[MAX_USER_NAME];
    STR Password[MAX_USER_NAME];
    STR PrivilegeStr[16];
    UINT AccountCount = 0;
    BOOL IsFirstUser = FALSE;
    UINT Result;
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

    // Check if this is the first user through the scripting exposure layer.
    if (!ShellGetAccountCount(Context, &AccountCount)) {
        ConsolePrint(TEXT("ERROR: Failed to query accounts\n"));
        return DF_RETURN_SUCCESS;
    }

    IsFirstUser = (AccountCount == 0);
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
    Result = RunEmbeddedAccountCreateScript(Context, UserName, Password, Privilege);
    if (Result != DF_RETURN_SUCCESS) {
        ShellClearHandledScriptError(Context);
        ConsolePrint(TEXT("ERROR: Failed to create user '%s'\n"), UserName);
    }

    return DF_RETURN_SUCCESS;
}

/***************************************************************************/

U32 CMD_deluser(LPSHELLCONTEXT Context) {
    STR UserName[MAX_USER_NAME];

    ParseNextCommandLineComponent(Context);
    if (StringLength(Context->Command) == 0) {
        ConsolePrint(TEXT("ERROR: Missing username argument\n"));
        ConsolePrint(TEXT("Usage: del_user <username>\n"));
        return DF_RETURN_SUCCESS;
    }
    StringCopy(UserName, Context->Command);

    if (RunEmbeddedAccountDeleteScript(Context, UserName) == DF_RETURN_SUCCESS) {
        ConsolePrint(TEXT("User '%s' deleted successfully\n"), UserName);
    } else {
        ShellClearHandledScriptError(Context);
        ConsolePrint(TEXT("Failed to delete user '%s'\n"), UserName);
    }

    return DF_RETURN_SUCCESS;
}

/***************************************************************************/

U32 CMD_login(LPSHELLCONTEXT Context) {
    STR UserName[MAX_USER_NAME];
    STR Password[MAX_USER_NAME];
    UINT WaitRemaining;
    BOOL IsLocked;


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

    LPUSER_ACCOUNT Account = FindAccount(UserName);
    if (Account == NULL) {
        Sleep(AUTH_POLICY_FAILURE_DELAY_MS);
        ConsolePrint(TEXT("ERROR: Invalid credentials\n"));
        return DF_RETURN_SUCCESS;
    }

    WaitRemaining = 0;
    if (!CanAttemptUserAuthentication(Account, &WaitRemaining)) {
        ConsolePrint(TEXT("ERROR: Too many attempts. Retry in %u ms\n"), (U32)WaitRemaining);
        return DF_RETURN_SUCCESS;
    }

    if (!VerifyPassword(Password, Account->PasswordHash)) {
        IsLocked = RecordUserAuthenticationFailure(Account, &WaitRemaining);
        if (IsLocked) {
            ConsolePrint(TEXT("ERROR: Too many attempts. Account locked for %u ms\n"), (U32)WaitRemaining);
        } else {
            ConsolePrint(TEXT("ERROR: Invalid credentials\n"));
        }
        return DF_RETURN_SUCCESS;
    }

    RecordUserAuthenticationSuccess(Account);

    LPUSER_SESSION Session = CreateUserSession(Account->UserID, (HANDLE)GetCurrentTask());
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

    LPUSER_SESSION Session = GetCurrentSession();
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

    LPUSER_SESSION Session = GetCurrentSession();
    if (Session == NULL) {
        ConsolePrint(TEXT("No active session\n"));
        return DF_RETURN_SUCCESS;
    }

    LPUSER_ACCOUNT Account = FindAccountByID(Session->UserID);
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
    STR OldPassword[MAX_PASSWORD];
    STR NewPassword[MAX_PASSWORD];
    STR ConfirmPassword[MAX_PASSWORD];

    ConsolePrint(TEXT("Password: "));
    CommandLineEditorReadLine(&Context->Input.Editor, Context->Input.CommandLine, sizeof Context->Input.CommandLine, TRUE);
    StringCopy(OldPassword, Context->Input.CommandLine);

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

    if (RunEmbeddedAccountChangePasswordScript(Context, OldPassword, NewPassword) == DF_RETURN_SUCCESS) {
        ConsolePrint(TEXT("Password changed successfully\n"));
    } else {
        ShellClearHandledScriptError(Context);
        ConsolePrint(TEXT("Failed to change password\n"));
    }

    return DF_RETURN_SUCCESS;
}

/***************************************************************************/
