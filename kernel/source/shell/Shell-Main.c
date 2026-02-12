#include "shell/Shell-Shared.h"

/************************************************************************/

static BOOL HandleUserLoginProcess(void) {
    // Check if any users exist
    LPLIST UserAccountList = GetUserAccountList();
    BOOL HasUsers = (UserAccountList != NULL && UserAccountList->First != NULL);

    if (!HasUsers) {
        // No users exist, prompt to create the first admin user
        ConsolePrint(TEXT("No existing user account. You need to create the first admin user.\n"));

        SHELLCONTEXT TempContext;
        InitShellContext(&TempContext);
        CMD_adduser(&TempContext);

        // Check if user was created successfully
        UserAccountList = GetUserAccountList();
        BOOL NewHasUsers = (UserAccountList != NULL && UserAccountList->First != NULL);

        if (NewHasUsers == FALSE) {
            ConsolePrint(TEXT("ERROR: Failed to create user account. System will exit.\n"));
            return FALSE;
        }
    }

    // Login loop - always required after user creation or if users exist
    ConsolePrint(TEXT("Login\n"));
    BOOL LoggedIn = FALSE;
    U32 LoginAttempts = 0;

    while (!LoggedIn && LoginAttempts < 5) {
        LoginAttempts++;

        SHELLCONTEXT TempContext;
        InitShellContext(&TempContext);
        CMD_login(&TempContext);

        // Check if login was successful
        LPUSERSESSION Session = GetCurrentSession();

        SAFE_USE(Session) {
            LoggedIn = TRUE;
            LPUSERACCOUNT Account = FindUserAccountByID(Session->UserID);

            SAFE_USE (Account) {
                ConsolePrint(TEXT("Logged in as: %s (%s)\n"), Account->UserName,
                    Account->Privilege == EXOS_PRIVILEGE_ADMIN ? TEXT("Administrator") : TEXT("User")
                    );
            }
        } else {
            ConsolePrint(TEXT("Login failed. Please try again. (Attempt %d/5)\n\n"), LoginAttempts);
        }
    }

    if (!LoggedIn) {
        ConsolePrint(TEXT("Too many failed login attempts.\n"));
        return FALSE;
    }

    return TRUE;
}

/************************************************************************/

/**
 * @brief Entry point for the interactive shell.
 *
 * Initializes the shell context, runs configured executables and processes
 * user commands until termination.
 *
 * @param Param Unused parameter.
 * @return Exit code of the shell.
 */
U32 Shell(LPVOID Param) {
    TRACED_FUNCTION;

    UNUSED(Param);
    SHELLCONTEXT Context;


    InitShellContext(&Context);

    if (GetDoLogin() && !HandleUserLoginProcess()) { return 0; }

    ExecuteStartupCommands();

    while (ParseCommand(&Context)) {
    }

    ConsolePrint(TEXT("Exiting shell\n"));

    DeinitShellContext(&Context);


    TRACED_EPILOGUE("Shell");
    return 1;
}
