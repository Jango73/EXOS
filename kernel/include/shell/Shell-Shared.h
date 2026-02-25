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


    Shell - Shared Definitions

\************************************************************************/

#ifndef SHELL_SHARED_H_INCLUDED
#define SHELL_SHARED_H_INCLUDED

#include "arch/Disassemble.h"
#include "Clock.h"
#include "Console.h"
#include "drivers/Keyboard.h"
#include "drivers/filesystems/NTFS.h"
#include "drivers/storage/NVMe-Core.h"
#include "drivers/storage/USBStorage.h"
#include "drivers/XHCI.h"
#include "DriverEnum.h"
#include "Endianness.h"
#include "Exposed.h"
#include "File.h"
#include "Kernel.h"
#include "Lang.h"
#include "Log.h"
#include "Memory-Descriptors.h"
#include "network/Network.h"
#include "network/NetworkManager.h"
#include "process/Process.h"
#include "Profile.h"
#include "script/Script.h"
#include "UserAccount.h"
#include "UserSession.h"
#include "utils/CommandLineEditor.h"
#include "utils/Helpers.h"
#include "utils/Path.h"
#include "utils/StringArray.h"
#include "VKey.h"

/************************************************************************/

#define SHELL_NUM_BUFFERS 8
#define BUFFER_SIZE 1024
#define HISTORY_SIZE 20

/************************************************************************/

typedef struct tag_SHELLINPUTSTATE {
    STR CommandLine[MAX_PATH_NAME];
    COMMANDLINEEDITOR Editor;
} SHELLINPUTSTATE, *LPSHELLINPUTSTATE;

typedef struct tag_SHELLCONTEXT {
    U32 Component;
    U32 CommandChar;
    SHELLINPUTSTATE Input;
    STR Command[256];
    STR CurrentFolder[MAX_PATH_NAME];
    LPVOID BufferBase;
    U32 BufferSize;
    LPSTR Buffer[SHELL_NUM_BUFFERS];
    STRINGARRAY Options;
    PATHCOMPLETION PathCompletion;
    LPSCRIPT_CONTEXT ScriptContext;
} SHELLCONTEXT, *LPSHELLCONTEXT;

typedef U32 (*SHELLCOMMAND)(LPSHELLCONTEXT Context);

typedef struct tag_SHELL_COMMAND_ENTRY {
    STR Name[MAX_COMMAND_NAME];
    STR AltName[MAX_COMMAND_NAME];
    STR Usage[MAX_COMMAND_NAME];
    SHELLCOMMAND Command;
} SHELL_COMMAND_ENTRY;

/************************************************************************/

extern SHELL_COMMAND_ENTRY COMMANDS[];

/************************************************************************/

void InitShellContext(LPSHELLCONTEXT Context);
void DeinitShellContext(LPSHELLCONTEXT Context);
void ClearOptions(LPSHELLCONTEXT Context);
BOOL ShowPrompt(LPSHELLCONTEXT Context);
BOOL ParseNextCommandLineComponent(LPSHELLCONTEXT Context);
BOOL HasOption(LPSHELLCONTEXT Context, LPCSTR ShortName, LPCSTR LongName);
BOOL QualifyFileName(LPSHELLCONTEXT Context, LPCSTR RawName, LPSTR FileName);
BOOL QualifyCommandLine(LPSHELLCONTEXT Context, LPCSTR RawCommandLine, LPSTR QualifiedCommandLine);
BOOL SpawnExecutable(LPSHELLCONTEXT Context, LPCSTR CommandName, BOOL Background);
BOOL RunScriptFile(LPSHELLCONTEXT Context, LPCSTR ScriptFileName);

void ExecuteStartupCommands(void);
void ExecuteCommandLine(LPSHELLCONTEXT Context, LPCSTR CommandLine);
BOOL ParseCommand(LPSHELLCONTEXT Context);

void ShellScriptOutput(LPCSTR Message, LPVOID UserData);
U32 ShellScriptExecuteCommand(LPCSTR Command, LPVOID UserData);
LPCSTR ShellScriptResolveVariable(LPCSTR VarName, LPVOID UserData);
U32 ShellScriptCallFunction(LPCSTR FuncName, LPCSTR Argument, LPVOID UserData);

U32 CMD_adduser(LPSHELLCONTEXT Context);
U32 CMD_login(LPSHELLCONTEXT Context);

void SystemDataViewMode(void);

/************************************************************************/

#endif
