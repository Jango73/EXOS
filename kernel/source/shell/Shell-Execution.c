
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


    Shell execution

\************************************************************************/

#include "shell/Shell-Shared.h"

/***************************************************************************/

/**
 * @brief Launch executables listed in the kernel configuration.
 *
 * Each [[Run]] item of exos.toml is checked and the command is executed
 * using the same pipeline as interactive shell commands.
 */
void ExecuteStartupCommands(void) {
    U32 ConfigIndex = 0;
    STR Key[MAX_USER_NAME];
    LPCSTR CommandLine;
    SHELLCONTEXT Context;


    // Wait 2 seconds for network stack to stabilize (ARP, etc.)
    Sleep(2000);

    LPTOML Configuration = GetConfiguration();
    if (Configuration == NULL) {
        return;
    }

    InitShellContext(&Context);

    FOREVER {
        StringPrintFormat(Key, TEXT("Run.%u.Command"), ConfigIndex);
        CommandLine = TomlGet(Configuration, Key);
        if (CommandLine == NULL) break;

        ExecuteCommandLine(&Context, CommandLine);

        ConfigIndex++;
    }

    DeinitShellContext(&Context);

}

/***************************************************************************/

/**
 * @brief Execute a command line string.
 *
 * Parses and executes a command line
 *
 * @param Context Shell context to use for execution
 * @param CommandLine Command line string to execute
 */
void ExecuteCommandLine(LPSHELLCONTEXT Context, LPCSTR CommandLine) {
    SAFE_USE_3(Context, Context->ScriptContext, CommandLine) {

        SCRIPT_ERROR Error = ScriptExecute(Context->ScriptContext, CommandLine);

        if (Error != SCRIPT_OK) {
            ConsolePrint(TEXT("Error: %s\n"), ScriptGetErrorMessage(Context->ScriptContext));
        }
    } else {
        ERROR(TEXT("[ExecuteCommandLine] Null pointer\n"));
    }
}

/***************************************************************************/

/**
 * @brief Parse and execute a single command line from user input.
 *
 * @param Context Shell context to fill and execute.
 * @return TRUE to continue the shell loop, FALSE otherwise.
 */
BOOL ParseCommand(LPSHELLCONTEXT Context) {

    ShowPrompt(Context);

    Context->Component = 0;
    Context->CommandChar = 0;
    MemorySet(Context->Input.CommandLine, 0, sizeof Context->Input.CommandLine);

    CommandLineEditorReadLine(&Context->Input.Editor, Context->Input.CommandLine, sizeof Context->Input.CommandLine, FALSE);

    if (Context->Input.CommandLine[0] != STR_NULL) {
        LPUSERSESSION Session = NULL;

        CommandLineEditorRemember(&Context->Input.Editor, Context->Input.CommandLine);
        ConsoleResetPaging();
        ExecuteCommandLine(Context, Context->Input.CommandLine);
        Session = GetCurrentSession();
        SAFE_USE_VALID_ID(Session, KOID_USERSESSION) { UpdateSessionActivity(Session); }
    }


    return TRUE;
}

/************************************************************************/

/**
 * @brief Shell callback for script output.
 * @param Message Message to output
 * @param UserData Shell context (unused)
 */
void ShellScriptOutput(LPCSTR Message, LPVOID UserData) {
    UNUSED(UserData);
    ConsolePrint(Message);
}

/************************************************************************/

/**
 * @brief Shell callback for script command execution.
 * @param Command Command to execute
 * @param UserData Shell context
 * @return DF_RETURN_SUCCESS on success or an error code on failure
 */
U32 ShellScriptExecuteCommand(LPCSTR Command, LPVOID UserData) {
    LPSHELLCONTEXT Context = (LPSHELLCONTEXT)UserData;
    U32 Index;
    U32 Result = DF_RETURN_GENERIC;

    if (Context == NULL || Command == NULL) {
        return DF_RETURN_BAD_PARAMETER;
    }


    StringCopy(Context->Input.CommandLine, Command);

    ClearOptions(Context);

    Context->Component = 0;
    Context->CommandChar = 0;

    ParseNextCommandLineComponent(Context);

    if (StringLength(Context->Command) == 0) {
        Result = DF_RETURN_SUCCESS;
        return Result;
    }

    {
        STR CommandName[MAX_FILE_NAME];
        StringCopy(CommandName, Context->Command);

        for (Index = 0; COMMANDS[Index].Command != NULL; Index++) {
            if (StringCompareNC(CommandName, COMMANDS[Index].Name) == 0 ||
                StringCompareNC(CommandName, COMMANDS[Index].AltName) == 0) {
                Result = COMMANDS[Index].Command(Context);
                return Result;
            }
        }

        if (SpawnExecutable(Context, Context->Input.CommandLine, FALSE) == TRUE) {
            Result = DF_RETURN_SUCCESS;
            return Result;
        }

        if (Context->ScriptContext) {
            Context->ScriptContext->ErrorCode = SCRIPT_ERROR_SYNTAX;
            StringPrintFormat(
                Context->ScriptContext->ErrorMessage,
                TEXT("Unknown command: %s"),
                CommandName);
        }
    }

    return Result;
}

/************************************************************************/

/**
 * @brief Shell callback for script variable resolution.
 * @param VarName Variable name to resolve
 * @param UserData Shell context (unused)
 * @return Variable value or NULL if not found
 */
LPCSTR ShellScriptResolveVariable(LPCSTR VarName, LPVOID UserData) {
    UNUSED(VarName);
    UNUSED(UserData);
    return NULL;
}

/************************************************************************/

/**
 * @brief Shell callback for script function calls.
 * @param FuncName Function name to call
 * @param Argument String argument for the function
 * @param UserData Shell context
 * @return Function result (U32)
 */
U32 ShellScriptCallFunction(LPCSTR FuncName, LPCSTR Argument, LPVOID UserData) {
    LPSHELLCONTEXT Context = (LPSHELLCONTEXT)UserData;

    if (STRINGS_EQUAL(FuncName, TEXT("exec"))) {
        if (Context == NULL || Argument == NULL) {
            return DF_RETURN_BAD_PARAMETER;
        }

        // Execute the provided command line using the standard shell command flow
        U32 Result = ShellScriptExecuteCommand(Argument, Context);
        return Result;
    } else if (STRINGS_EQUAL(FuncName, TEXT("print"))) {
        ConsolePrint(Argument);
        return 0;
    }

    return MAX_U32;
}
