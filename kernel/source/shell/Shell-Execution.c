
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

#include "shell/Shell-Commands-Private.h"
#include "ID.h"
#include "SYSCall.h"

/***************************************************************************/

/**
 * @brief Check if a script text already contains a line break.
 * @param Text Text to inspect.
 * @return TRUE when the text contains '\r' or '\n', FALSE otherwise.
 */
static BOOL ShellScriptContainsLineBreak(LPCSTR Text) {
    if (Text == NULL) {
        return FALSE;
    }

    if (StringFindChar(Text, '\n') != NULL) {
        return TRUE;
    }

    if (StringFindChar(Text, '\r') != NULL) {
        return TRUE;
    }

    return FALSE;
}

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
        LPUSER_SESSION Session = NULL;

        CommandLineEditorRemember(&Context->Input.Editor, Context->Input.CommandLine);
        ConsoleResetPaging();
        ExecuteCommandLine(Context, Context->Input.CommandLine);
        Session = GetCurrentSession();
        SAFE_USE_VALID_ID(Session, KOID_USER_SESSION) { UpdateSessionActivity(Session); }
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
UINT ShellScriptExecuteCommand(LPCSTR Command, LPVOID UserData) {
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
 * @brief Concatenate script callback arguments into one shell-style string.
 * @param Context Shell context used for buffer storage.
 * @param ArgumentCount Number of arguments to concatenate.
 * @param Arguments Argument vector.
 * @return Pointer to an internal shell buffer, or NULL on failure.
 */
static LPCSTR ShellScriptJoinArguments(
    LPSHELLCONTEXT Context,
    UINT ArgumentCount,
    LPCSTR* Arguments) {
    UINT BufferIndex = 0;
    UINT Index;

    if (Context == NULL || Context->Buffer[0] == NULL || ArgumentCount == 0 || Arguments == NULL) {
        return NULL;
    }

    for (Index = 0; Index < ArgumentCount; Index++) {
        LPCSTR Argument = Arguments[Index];
        U32 ArgumentLength;

        if (Argument == NULL) {
            Argument = TEXT("");
        }

        ArgumentLength = StringLength(Argument);
        if (BufferIndex + ArgumentLength + 2 >= BUFFER_SIZE) {
            return NULL;
        }

        if (Index > 0) {
            Context->Buffer[0][BufferIndex++] = STR_SPACE;
        }

        MemoryCopy(Context->Buffer[0] + BufferIndex, Argument, ArgumentLength);
        BufferIndex += ArgumentLength;
    }

    Context->Buffer[0][BufferIndex] = STR_NULL;
    return Context->Buffer[0];
}

/************************************************************************/

/**
 * @brief Store one script error for a shell host function and return failure sentinel.
 * @param Context Shell context owning the script context.
 * @param ErrorCode Script error code to store.
 * @param Message Human-readable error message.
 * @return SCRIPT_FUNCTION_STATUS_ERROR.
 */
static INT ShellScriptFailFunction(
    LPSHELLCONTEXT Context,
    SCRIPT_ERROR ErrorCode,
    LPCSTR Message) {
    if (Context != NULL && Context->ScriptContext != NULL) {
        Context->ScriptContext->ErrorCode = ErrorCode;
        if (Message != NULL) {
            StringCopy(Context->ScriptContext->ErrorMessage, Message);
        }
    }

    return SCRIPT_FUNCTION_STATUS_ERROR;
}

/************************************************************************/

/**
 * @brief Kill one process or task referenced by a user-visible handle.
 * @param Context Shell context owning the script context.
 * @param HandleValue Serialized handle value.
 * @return Non-zero on success or SCRIPT_FUNCTION_STATUS_ERROR on failure.
 */
static INT ShellScriptKillHandle(LPSHELLCONTEXT Context, LPCSTR HandleValue) {
    UINT Handle = 0;
    LINEAR ObjectPointer = 0;
    LPOBJECT Object = NULL;
    UINT Status = 0;

    if (HandleValue == NULL || StringLength(HandleValue) == 0) {
        return ShellScriptFailFunction(Context, SCRIPT_ERROR_SYNTAX, TEXT("kill(handle) expects one handle argument"));
    }

    Handle = StringToU32(HandleValue);
    if (Handle < HANDLE_MINIMUM) {
        return ShellScriptFailFunction(Context, SCRIPT_ERROR_TYPE_MISMATCH, TEXT("kill(handle) expects a valid handle"));
    }

    ObjectPointer = HandleToPointer((HANDLE)Handle);
    if (ObjectPointer == 0) {
        return ShellScriptFailFunction(Context, SCRIPT_ERROR_UNDEFINED_VAR, TEXT("kill(handle) received an unknown handle"));
    }

    Object = (LPOBJECT)ObjectPointer;
    SAFE_USE_VALID(Object) {
        if (Object->TypeID == KOID_PROCESS) {
            Status = SysCall_KillProcess(Handle);
            if (Status == 0) {
                return ShellScriptFailFunction(Context, SCRIPT_ERROR_UNAUTHORIZED, TEXT("kill(handle) failed to terminate the process"));
            }
            return (INT)Status;
        }

        if (Object->TypeID == KOID_TASK) {
            Status = SysCall_KillTask(Handle);
            if (Status == 0) {
                return ShellScriptFailFunction(Context, SCRIPT_ERROR_UNAUTHORIZED, TEXT("kill(handle) failed to terminate the task"));
            }
            return (INT)Status;
        }
    }

    return ShellScriptFailFunction(Context, SCRIPT_ERROR_TYPE_MISMATCH, TEXT("kill(handle) only supports process or task handles"));
}

/************************************************************************/

/**
 * @brief Parse one positive integer argument for a shell host function.
 * @param Context Shell context owning the script state.
 * @param FunctionName Name of the host function.
 * @param ParameterName Logical parameter name.
 * @param ValueText Serialized argument text.
 * @param OutValue Parsed integer value.
 * @return TRUE on success.
 */
static BOOL ShellScriptParsePositiveInteger(
    LPSHELLCONTEXT Context,
    LPCSTR FunctionName,
    LPCSTR ParameterName,
    LPCSTR ValueText,
    U32* OutValue) {
    U32 Index = 0;
    U32 Value = 0;

    if (OutValue == NULL) {
        return FALSE;
    }

    if (ValueText == NULL || StringLength(ValueText) == 0) {
        ShellScriptFailFunction(Context, SCRIPT_ERROR_TYPE_MISMATCH, TEXT("set_graphics_driver() expects positive integer arguments"));
        return FALSE;
    }

    for (Index = 0; ValueText[Index] != STR_NULL; Index++) {
        if (!IsNumeric(ValueText[Index])) {
            STR Message[MAX_ERROR_MESSAGE];

            StringPrintFormat(
                Message,
                TEXT("%s() expects %s to be a positive integer"),
                FunctionName,
                ParameterName);
            ShellScriptFailFunction(Context, SCRIPT_ERROR_TYPE_MISMATCH, Message);
            return FALSE;
        }
    }

    Value = StringToU32(ValueText);
    if (Value == 0) {
        STR Message[MAX_ERROR_MESSAGE];

        StringPrintFormat(
            Message,
            TEXT("%s() expects %s to be a positive integer"),
            FunctionName,
            ParameterName);
        ShellScriptFailFunction(Context, SCRIPT_ERROR_TYPE_MISMATCH, Message);
        return FALSE;
    }

    *OutValue = Value;
    return TRUE;
}

/************************************************************************/

/**
 * @brief Shell callback for script function calls.
 * @param FuncName Function name to call
 * @param ArgumentCount Number of stringified arguments
 * @param Arguments String arguments for the function
 * @param UserData Shell context
 * @return Function result (U32)
 */
INT ShellScriptCallFunction(LPCSTR FuncName, UINT ArgumentCount, LPCSTR* Arguments, LPVOID UserData) {
    LPSHELLCONTEXT Context = (LPSHELLCONTEXT)UserData;
    LPCSTR JoinedArguments;

    if (STRINGS_EQUAL(FuncName, TEXT("exec"))) {
        if (Context == NULL || ArgumentCount == 0 || Arguments == NULL) {
            return DF_RETURN_BAD_PARAMETER;
        }

        JoinedArguments = ShellScriptJoinArguments(Context, ArgumentCount, Arguments);
        if (JoinedArguments == NULL) {
            return DF_RETURN_GENERIC;
        }

        // Execute the provided command line using the standard shell command flow
        INT Result = (INT)ShellScriptExecuteCommand(JoinedArguments, Context);
        return Result;
    } else if (STRINGS_EQUAL(FuncName, TEXT("print"))) {
        if (ArgumentCount == 0 || Arguments == NULL) {
            return DF_RETURN_BAD_PARAMETER;
        }

        JoinedArguments = ShellScriptJoinArguments(Context, ArgumentCount, Arguments);
        if (JoinedArguments == NULL) {
            return DF_RETURN_GENERIC;
        }

        ConsolePrint(TEXT("%s"), JoinedArguments);
        if (!ShellScriptContainsLineBreak(JoinedArguments)) {
            ConsolePrint(TEXT("\r\n"));
        }
        return 0;
    } else if (STRINGS_EQUAL(FuncName, TEXT("kill"))) {
        if (ArgumentCount != 1 || Arguments == NULL) {
            return ShellScriptFailFunction(Context, SCRIPT_ERROR_SYNTAX, TEXT("kill(handle) expects exactly one handle argument"));
        }

        return ShellScriptKillHandle(Context, Arguments[0]);
    } else if (STRINGS_EQUAL(FuncName, TEXT("set_graphics_driver"))) {
        STR ErrorMessage[MAX_ERROR_MESSAGE];
        U32 Width = 0;
        U32 Height = 0;
        U32 BitsPerPixel = 0;
        UINT Status = 0;

        if (ArgumentCount != 4 || Arguments == NULL) {
            return ShellScriptFailFunction(
                Context,
                SCRIPT_ERROR_SYNTAX,
                TEXT("set_graphics_driver(driver_alias, width, height, bpp) expects exactly four arguments"));
        }

        if (Arguments[0] == NULL || StringLength(Arguments[0]) == 0) {
            return ShellScriptFailFunction(
                Context,
                SCRIPT_ERROR_TYPE_MISMATCH,
                TEXT("set_graphics_driver() expects a non-empty driver alias"));
        }

        if (!ShellScriptParsePositiveInteger(Context, TEXT("set_graphics_driver"), TEXT("width"), Arguments[1], &Width) ||
            !ShellScriptParsePositiveInteger(Context, TEXT("set_graphics_driver"), TEXT("height"), Arguments[2], &Height) ||
            !ShellScriptParsePositiveInteger(Context, TEXT("set_graphics_driver"), TEXT("bpp"), Arguments[3], &BitsPerPixel)) {
            return SCRIPT_FUNCTION_STATUS_ERROR;
        }

        Status = ShellSetGraphicsDriver(
            Arguments[0],
            Width,
            Height,
            BitsPerPixel,
            ErrorMessage,
            sizeof(ErrorMessage));
        if (Status != DF_RETURN_SUCCESS) {
            return ShellScriptFailFunction(
                Context,
                SCRIPT_ERROR_TYPE_MISMATCH,
                ErrorMessage[0] != STR_NULL ? ErrorMessage : TEXT("set_graphics_driver() failed"));
        }

        return (INT)Status;
    }

    return SCRIPT_FUNCTION_STATUS_UNKNOWN;
}
