
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


    Shell

\************************************************************************/

#include "drivers/ACPI.h"
#include "Base.h"
#include "Clock.h"
#include "utils/CommandLineEditor.h"
#include "Console.h"
#include "Disk.h"
#include "Endianness.h"
#include "Exposed.h"
#include "File.h"
#include "FileSystem.h"
#include "GFX.h"
#include "Heap.h"
#include "utils/Helpers.h"
#include "Kernel.h"
#include "drivers/Keyboard.h"
#include "List.h"
#include "Log.h"
#include "arch/Disassemble.h"
#include "Network.h"
#include "NetworkManager.h"
#include "utils/Path.h"
#include "Process.h"
#include "Script.h"
#include "CoreString.h"
#include "utils/StringArray.h"
#include "System.h"
#include "User.h"
#include "UserAccount.h"
#include "UserSession.h"
#include "VKey.h"

/************************************************************************/

#define SHELL_NUM_BUFFERS 8
#define BUFFER_SIZE 1024
#define HISTORY_SIZE 20

/************************************************************************/
// Shell input state

typedef struct tag_SHELLINPUTSTATE {
    STR CommandLine[MAX_PATH_NAME];
    COMMANDLINEEDITOR Editor;
} SHELLINPUTSTATE, *LPSHELLINPUTSTATE;

/************************************************************************/
// The shell context

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

/************************************************************************/
// The shell command functions

typedef U32 (*SHELLCOMMAND)(LPSHELLCONTEXT);

static U32 CMD_commands(LPSHELLCONTEXT);
static U32 CMD_cls(LPSHELLCONTEXT);
static U32 CMD_dir(LPSHELLCONTEXT);
static U32 CMD_cd(LPSHELLCONTEXT);
static U32 CMD_md(LPSHELLCONTEXT);
static U32 CMD_run(LPSHELLCONTEXT);
static U32 CMD_exit(LPSHELLCONTEXT);
static U32 CMD_sysinfo(LPSHELLCONTEXT);
static U32 CMD_killtask(LPSHELLCONTEXT);
static U32 CMD_showprocess(LPSHELLCONTEXT);
static U32 CMD_showtask(LPSHELLCONTEXT);
static U32 CMD_memedit(LPSHELLCONTEXT);
static U32 CMD_disasm(LPSHELLCONTEXT);
static U32 CMD_cat(LPSHELLCONTEXT);
static U32 CMD_copy(LPSHELLCONTEXT);
static U32 CMD_edit(LPSHELLCONTEXT);
static U32 CMD_hd(LPSHELLCONTEXT);
static U32 CMD_filesystem(LPSHELLCONTEXT);
static U32 CMD_network(LPSHELLCONTEXT);
static U32 CMD_pic(LPSHELLCONTEXT);
static U32 CMD_outp(LPSHELLCONTEXT);
static U32 CMD_inp(LPSHELLCONTEXT);
static U32 CMD_reboot(LPSHELLCONTEXT);
static U32 CMD_shutdown(LPSHELLCONTEXT);
static U32 CMD_adduser(LPSHELLCONTEXT);
static U32 CMD_deluser(LPSHELLCONTEXT);
static U32 CMD_login(LPSHELLCONTEXT);
static U32 CMD_logout(LPSHELLCONTEXT);
static U32 CMD_whoami(LPSHELLCONTEXT);
static U32 CMD_passwd(LPSHELLCONTEXT);

static void ShellScriptOutput(LPCSTR Message, LPVOID UserData);
static U32 ShellScriptExecuteCommand(LPCSTR Command, LPVOID UserData);
static LPCSTR ShellScriptResolveVariable(LPCSTR VarName, LPVOID UserData);
static U32 ShellScriptCallFunction(LPCSTR FuncName, LPCSTR Argument, LPVOID UserData);
static BOOL ShellCommandLineCompletion(
    const COMMANDLINE_COMPLETION_CONTEXT* CompletionContext,
    LPSTR Output,
    U32 OutputSize);
static void ShellRegisterScriptHostObjects(LPSHELLCONTEXT Context);
static void ClearOptions(LPSHELLCONTEXT);
static BOOL HasOption(LPSHELLCONTEXT, LPCSTR, LPCSTR);
static void ListDirectory(LPSHELLCONTEXT, LPCSTR, U32, BOOL, BOOL, U32*);
static BOOL QualifyFileName(LPSHELLCONTEXT, LPCSTR, LPSTR);
static BOOL QualifyCommandLine(LPSHELLCONTEXT, LPCSTR, LPSTR);
static void ExecuteStartupCommands(void);
static void ExecuteCommandLine(LPSHELLCONTEXT Context, LPCSTR CommandLine);
static BOOL SpawnExecutable(LPSHELLCONTEXT, LPCSTR, BOOL);

/************************************************************************/
// The shell command table

static struct {
    STR Name[32];
    STR AltName[32];
    STR Usage[32];
    SHELLCOMMAND Command;
} COMMANDS[] = {
    {"commands", "help", "", CMD_commands},
    {"clear", "cls", "", CMD_cls},
    {"ls", "dir", "[Name] [-p] [-r]", CMD_dir},
    {"cd", "cd", "Name", CMD_cd},
    {"mkdir", "md", "Name", CMD_md},
    {"run", "launch", "Name [-b|--background]", CMD_run},
    {"quit", "exit", "", CMD_exit},
    {"sys", "sysinfo", "", CMD_sysinfo},
    {"kill", "killtask", "Number", CMD_killtask},
    {"process", "showprocess", "Number", CMD_showprocess},
    {"task", "showtask", "Number", CMD_showtask},
    {"mem", "memedit", "Address", CMD_memedit},
    {"dis", "disasm", "Address InstructionCount", CMD_disasm},
    {"cat", "type", "", CMD_cat},
    {"cp", "copy", "", CMD_copy},
    {"edit", "edit", "Name", CMD_edit},
    {"hd", "hd", "", CMD_hd},
    {"fs", "filesystem", "", CMD_filesystem},
    {"net", "network", "", CMD_network},
    {"pic", "pic", "", CMD_pic},
    {"outp", "outp", "", CMD_outp},
    {"inp", "inp", "", CMD_inp},
    {"reboot", "reboot", "", CMD_reboot},
    {"shutdown", "poweroff", "", CMD_shutdown},
    {"adduser", "newuser", "username", CMD_adduser},
    {"deluser", "deleteuser", "username", CMD_deluser},
    {"login", "login", "", CMD_login},
    {"logout", "logout", "", CMD_logout},
    {"whoami", "who", "", CMD_whoami},
    {"passwd", "setpassword", "", CMD_passwd},
    {"", "", "", NULL},
};

/************************************************************************/

static void ShellRegisterScriptHostObjects(LPSHELLCONTEXT Context) {

    if (Context == NULL || Context->ScriptContext == NULL) {
        return;
    }

    SAFE_USE(Kernel.Process) {
        if (!ScriptRegisterHostSymbol(
                Context->ScriptContext,
                TEXT("process"),
                SCRIPT_HOST_SYMBOL_ARRAY,
                Kernel.Process,
                &ProcessArrayDescriptor,
                NULL)) {
            DEBUG(TEXT("[ShellRegisterScriptHostObjects] Failed to register process host symbol"));
        }
    }
}

/************************************************************************/

static void InitShellContext(LPSHELLCONTEXT This) {
    U32 Index;

    MemorySet(This, 0, sizeof(SHELLCONTEXT));

    DEBUG(TEXT("[InitShellContext] Enter"));

    This->Component = 0;
    This->CommandChar = 0;

    CommandLineEditorInit(&This->Input.Editor, HISTORY_SIZE);
    CommandLineEditorSetCompletionCallback(
        &This->Input.Editor,
        ShellCommandLineCompletion,
        This);
    StringArrayInit(&This->Options, 8);
    PathCompletionInit(&This->PathCompletion, GetSystemFS());

    for (Index = 0; Index < SHELL_NUM_BUFFERS; Index++) {
        This->Buffer[Index] = (LPSTR)HeapAlloc(BUFFER_SIZE);
    }

    {
        STR Root[2] = {PATH_SEP, STR_NULL};
        StringCopy(This->CurrentFolder, Root);
    }

    // Initialize persistent script context
    SCRIPT_CALLBACKS Callbacks = {
        ShellScriptOutput,
        ShellScriptExecuteCommand,
        ShellScriptResolveVariable,
        ShellScriptCallFunction,
        This
    };
    This->ScriptContext = ScriptCreateContext(&Callbacks);

    ShellRegisterScriptHostObjects(This);

    DEBUG(TEXT("[InitShellContext] Exit"));
}

/***************************************************************************/

static void DeinitShellContext(LPSHELLCONTEXT This) {
    U32 Index;

    DEBUG(TEXT("[DeinitShellContext] Enter"));

    for (Index = 0; Index < SHELL_NUM_BUFFERS; Index++) {
        if (This->Buffer[Index]) HeapFree(This->Buffer[Index]);
    }

    CommandLineEditorDeinit(&This->Input.Editor);
    StringArrayDeinit(&This->Options);
    PathCompletionDeinit(&This->PathCompletion);

    // Cleanup persistent script context
    if (This->ScriptContext) {
        ScriptDestroyContext(This->ScriptContext);
        This->ScriptContext = NULL;
    }

    DEBUG(TEXT("[DeinitShellContext] Exit"));
}

/***************************************************************************/

static void ClearOptions(LPSHELLCONTEXT Context) {
    U32 Index;
    for (Index = 0; Index < Context->Options.Count; Index++) {
        if (Context->Options.Items[Index]) HeapFree(Context->Options.Items[Index]);
    }
    Context->Options.Count = 0;
}

/***************************************************************************/

/*
static void RotateBuffers(LPSHELLCONTEXT This) {
    U32 Index = 0;

    if (This->BufferBase) {
        for (Index = 1; Index < SHELL_NUM_BUFFERS; Index++) {
            MemoryCopy(This->Buffer[Index - 1], This->Buffer[Index],
                       BUFFER_SIZE);
        }
        MemoryCopy(This->Buffer[SHELL_NUM_BUFFERS - 1], This->Input.CommandLine,
                   BUFFER_SIZE);
    }
}
*/

/***************************************************************************/

static BOOL ShowPrompt(LPSHELLCONTEXT Context) {
    ConsolePrint(TEXT("%s>"), Context->CurrentFolder);
    return TRUE;
}

/***************************************************************************/

static BOOL ParseNextCommandLineComponent(LPSHELLCONTEXT Context) {
    U32 Quotes = 0;
    U32 d = 0;

    Context->Command[d] = STR_NULL;

    if (Context->Input.CommandLine[Context->CommandChar] == STR_NULL) return TRUE;

    while (Context->Input.CommandLine[Context->CommandChar] != STR_NULL &&
           Context->Input.CommandLine[Context->CommandChar] <= STR_SPACE) {
        Context->CommandChar++;
    }

    FOREVER {
        if (Context->Input.CommandLine[Context->CommandChar] == STR_NULL) {
            break;
        } else if (Context->Input.CommandLine[Context->CommandChar] <= STR_SPACE) {
            if (Quotes == 0) {
                Context->CommandChar++;
                break;
            }
        } else if (Context->Input.CommandLine[Context->CommandChar] == STR_QUOTE) {
            Context->CommandChar++;
            if (Quotes == 0)
                Quotes = 1;
            else
                break;
        }

        Context->Command[d] = Context->Input.CommandLine[Context->CommandChar];

        Context->CommandChar++;
        d++;

        // Prevent buffer overflow
        if (d >= 255) {
            break;
        }
    }

    Context->Component++;
    Context->Command[d] = STR_NULL;

    if (Context->Command[0] == STR_MINUS) {
        U32 Offset = 1;
        if (Context->Command[1] == STR_MINUS) Offset = 2;
        if (Context->Command[Offset] != STR_NULL) {
            StringArrayAddUnique(&Context->Options, Context->Command + Offset);
        }
        return ParseNextCommandLineComponent(Context);
    }

    return TRUE;
}

/***************************************************************************/

static BOOL HasOption(LPSHELLCONTEXT Context, LPCSTR ShortName, LPCSTR LongName) {
    U32 Index;
    LPCSTR Option;
    for (Index = 0; Index < Context->Options.Count; Index++) {
        Option = StringArrayGet(&Context->Options, Index);
        if (ShortName && StringCompareNC(Option, ShortName) == 0) return TRUE;
        if (LongName && StringCompareNC(Option, LongName) == 0) return TRUE;
    }
    return FALSE;
}

/***************************************************************************/

/**
 * @brief Provide path-based completion for the command line editor.
 * @param CompletionContext Details about the token to complete.
 * @param Output Buffer receiving the replacement token.
 * @param OutputSize Size of the output buffer in characters.
 * @return TRUE when a completion was produced, FALSE otherwise.
 */
static BOOL ShellCommandLineCompletion(
    const COMMANDLINE_COMPLETION_CONTEXT* CompletionContext,
    LPSTR Output,
    U32 OutputSize) {
    LPSHELLCONTEXT Context;
    STR Token[MAX_PATH_NAME];
    STR Full[MAX_PATH_NAME];
    STR Completed[MAX_PATH_NAME];
    STR Display[MAX_PATH_NAME];
    STR Temp[MAX_PATH_NAME];
    U32 DisplayLength;

    if (CompletionContext == NULL) return FALSE;
    if (Output == NULL) return FALSE;
    if (OutputSize == 0) return FALSE;

    Context = (LPSHELLCONTEXT)CompletionContext->UserData;
    if (Context == NULL) return FALSE;

    if (CompletionContext->TokenLength >= MAX_PATH_NAME) return FALSE;

    StringCopyNum(Token, CompletionContext->Token, CompletionContext->TokenLength);
    Token[CompletionContext->TokenLength] = STR_NULL;

    if (Token[0] == PATH_SEP) {
        StringCopy(Full, Token);
    } else {
        if (!QualifyFileName(Context, Token, Full)) return FALSE;
    }

    if (!PathCompletionNext(&Context->PathCompletion, Full, Completed)) {
        return FALSE;
    }

    if (Token[0] == PATH_SEP) {
        StringCopy(Display, Completed);
    } else {
        U32 FolderLength = StringLength(Context->CurrentFolder);
        StringCopyNum(Temp, Completed, FolderLength);
        Temp[FolderLength] = STR_NULL;
        if (StringCompareNC(Temp, Context->CurrentFolder) == 0) {
            STR* DisplayPtr = Completed + FolderLength;
            if (DisplayPtr[0] == PATH_SEP) DisplayPtr++;
            StringCopy(Display, DisplayPtr);
        } else {
            StringCopy(Display, Completed);
        }
    }

    DisplayLength = StringLength(Display);
    if (DisplayLength >= OutputSize) return FALSE;

    StringCopy(Output, Display);

    return TRUE;
}

/***************************************************************************/

static BOOL QualifyFileName(LPSHELLCONTEXT Context, LPCSTR RawName, LPSTR FileName) {
    STR Sep[2] = {PATH_SEP, STR_NULL};
    STR Temp[MAX_PATH_NAME];
    LPSTR Ptr;
    LPSTR Token;
    U32 Length;
    STR Save;

    if (RawName[0] == PATH_SEP) {
        StringCopy(Temp, RawName);
    } else {
        StringCopy(Temp, Context->CurrentFolder);
        if (Temp[StringLength(Temp) - 1] != PATH_SEP) StringConcat(Temp, Sep);
        StringConcat(Temp, TEXT(RawName));
    }

    FileName[0] = PATH_SEP;
    FileName[1] = STR_NULL;

    Ptr = Temp;
    if (Ptr[0] == PATH_SEP) Ptr++;

    while (*Ptr) {
        Token = Ptr;
        while (*Ptr && *Ptr != PATH_SEP) Ptr++;
        Length = Ptr - Token;

        if (Length == 1 && Token[0] == STR_DOT) {
            // Skip current directory component
        } else if (Length == 2 && Token[0] == STR_DOT && Token[1] == STR_DOT) {
            // Remove previous component while preserving root
            LPSTR Slash = StringFindCharR(FileName, PATH_SEP);
            if (Slash) {
                if (Slash != FileName)
                    *Slash = STR_NULL;
                else
                    FileName[1] = STR_NULL;
            }
        } else if (Length > 0) {
            if (StringLength(FileName) > 1) StringConcat(FileName, Sep);
            Save = Token[Length];
            Token[Length] = STR_NULL;
            StringConcat(FileName, Token);
            Token[Length] = Save;
        }

        if (*Ptr == PATH_SEP) Ptr++;
    }

    return TRUE;
}

/***************************************************************************/

static BOOL QualifyCommandLine(LPSHELLCONTEXT Context, LPCSTR RawCommandLine, LPSTR QualifiedCommandLine) {
    U32 Quotes = 0;
    U32 s = 0;  // source index
    U32 d = 0;  // destination index
    STR ExecutableName[MAX_PATH_NAME];
    STR QualifiedPath[MAX_PATH_NAME];
    U32 e = 0;  // executable name index
    BOOL InExecutableName = TRUE;

    QualifiedCommandLine[0] = STR_NULL;

    // Skip leading spaces
    while (RawCommandLine[s] != STR_NULL && RawCommandLine[s] <= STR_SPACE) {
        s++;
    }

    if (RawCommandLine[s] == STR_NULL) return FALSE;

    // Parse the executable name (first word, handling quotes)
    while (RawCommandLine[s] != STR_NULL && InExecutableName) {
        if (RawCommandLine[s] == STR_QUOTE) {
            if (Quotes == 0) {
                Quotes = 1;
            } else {
                Quotes = 0;
                InExecutableName = FALSE;
            }
        } else if (RawCommandLine[s] <= STR_SPACE && Quotes == 0) {
            InExecutableName = FALSE;
        } else {
            if (e < MAX_PATH_NAME - 1) {
                ExecutableName[e++] = RawCommandLine[s];
            }
        }
        if (InExecutableName || RawCommandLine[s] == STR_QUOTE) {
            s++;
        }
    }
    ExecutableName[e] = STR_NULL;

    // Qualify the executable name
    if (!QualifyFileName(Context, ExecutableName, QualifiedPath)) {
        return FALSE;
    }

    // Build the qualified command line
    StringCopy(QualifiedCommandLine, QualifiedPath);
    d = StringLength(QualifiedCommandLine);

    // Copy the rest of the command line (arguments)
    if (RawCommandLine[s] != STR_NULL) {
        QualifiedCommandLine[d++] = STR_SPACE;
        while (RawCommandLine[s] != STR_NULL && d < MAX_PATH_NAME - 1) {
            QualifiedCommandLine[d++] = RawCommandLine[s++];
        }
    }
    QualifiedCommandLine[d] = STR_NULL;

    return TRUE;
}

/***************************************************************************/

static void ChangeFolder(LPSHELLCONTEXT Context) {
    FS_PATHCHECK Control;
    STR NewPath[MAX_PATH_NAME];

    ParseNextCommandLineComponent(Context);

    if (StringLength(Context->Command) == 0) {
        ConsolePrint(TEXT("Missing argument\n"));
        return;
    }

    if (QualifyFileName(Context, Context->Command, NewPath) == 0) return;

    Control.CurrentFolder[0] = STR_NULL;
    StringCopy(Control.SubFolder, NewPath);

    if (GetSystemFS()->Driver->Command(DF_FS_PATHEXISTS, (UINT)&Control)) {
        StringCopy(Context->CurrentFolder, NewPath);
    } else {
        ConsolePrint(TEXT("Unknown folder : %s\n"), NewPath);
    }
}

/***************************************************************************/

static void MakeFolder(LPSHELLCONTEXT Context) {
    LPFILESYSTEM FileSystem;
    FILEINFO FileInfo;
    STR FileName[MAX_PATH_NAME];

    ParseNextCommandLineComponent(Context);

    if (StringLength(Context->Command) == 0) {
        ConsolePrint(TEXT("Missing argument\n"));
        return;
    }

    FileSystem = GetSystemFS();
    if (FileSystem == NULL) return;

    if (QualifyFileName(Context, Context->Command, FileName)) {
        FileInfo.Size = sizeof(FILEINFO);
        FileInfo.FileSystem = FileSystem;
        FileInfo.Attributes = MAX_U32;
        StringCopy(FileInfo.Name, FileName);
        FileSystem->Driver->Command(DF_FS_CREATEFOLDER, (UINT)&FileInfo);
    }
}

/***************************************************************************/

static void ListFile(LPFILE File, U32 Indent) {
    STR Name[MAX_FILE_NAME];
    U32 MaxWidth = 80;
    U32 Length;
    U32 Index;

    DEBUG(TEXT("[ListFile] Processing file: %s"), File->Name);

    //-------------------------------------
    // Eliminate the . and .. files

    if (StringCompare(File->Name, TEXT(".")) == 0) return;
    if (StringCompare(File->Name, TEXT("..")) == 0) return;

    DEBUG(TEXT("[ListFile] Displaying file: %s"), File->Name);

    StringCopy(Name, File->Name);

    if (StringLength(Name) > ((MaxWidth - Indent) / 2)) {
        Index = ((MaxWidth - Indent) / 2) - 4;
        Name[Index++] = STR_DOT;
        Name[Index++] = STR_DOT;
        Name[Index++] = STR_DOT;
        Name[Index++] = STR_NULL;
    }

    Length = ((MaxWidth - Indent) / 2) - StringLength(Name);

    // Print name

    for (Index = 0; Index < Indent; Index++) ConsolePrint(TEXT(" "));
    ConsolePrint(Name);
    for (Index = 0; Index < Length; Index++) ConsolePrint(TEXT(" "));

    // Print size

    if (File->Attributes & FS_ATTR_FOLDER) {
        ConsolePrint(TEXT("%12s"), TEXT("<Folder>"));
    } else {
        ConsolePrint(TEXT("%12d"), File->SizeLow);
    }

    ConsolePrint(
        TEXT(" %d-%d-%d %d:%d "), (I32)File->Creation.Day, (I32)File->Creation.Month, (I32)File->Creation.Year,
        (I32)File->Creation.Hour, (I32)File->Creation.Minute);

    // Print attributes

    if (File->Attributes & FS_ATTR_READONLY)
        ConsolePrint(TEXT("R"));
    else
        ConsolePrint(TEXT("-"));
    if (File->Attributes & FS_ATTR_HIDDEN)
        ConsolePrint(TEXT("H"));
    else
        ConsolePrint(TEXT("-"));
    if (File->Attributes & FS_ATTR_SYSTEM)
        ConsolePrint(TEXT("S"));
    else
        ConsolePrint(TEXT("-"));
    if (File->Attributes & FS_ATTR_EXECUTABLE)
        ConsolePrint(TEXT("X"));
    else
        ConsolePrint(TEXT("-"));

    ConsolePrint(Text_NewLine);
}

/***************************************************************************/

static void ListDirectory(LPSHELLCONTEXT Context, LPCSTR Base, U32 Indent, BOOL Pause, BOOL Recurse, U32* NumListed) {
    FILEINFO Find;
    LPFILESYSTEM FileSystem;
    LPFILE File;
    STR Pattern[MAX_PATH_NAME];
    STR Sep[2] = {PATH_SEP, STR_NULL};

    UNUSED(Context);
    FileSystem = GetSystemFS();

    Find.Size = sizeof(FILEINFO);
    Find.FileSystem = FileSystem;
    Find.Attributes = MAX_U32;

    StringCopy(Pattern, Base);
    if (Pattern[StringLength(Pattern) - 1] != PATH_SEP) StringConcat(Pattern, Sep);
    StringConcat(Pattern, TEXT("*"));
    StringCopy(Find.Name, Pattern);

    File = (LPFILE)FileSystem->Driver->Command(DF_FS_OPENFILE, (UINT)&Find);
    if (File == NULL) {
        StringCopy(Find.Name, Base);
        File = (LPFILE)FileSystem->Driver->Command(DF_FS_OPENFILE, (UINT)&Find);
        if (File == NULL) {
            ConsolePrint(TEXT("Unknown file : %s\n"), Base);
            return;
        }
        ListFile(File, Indent);
        FileSystem->Driver->Command(DF_FS_CLOSEFILE, (UINT)File);
        return;
    }

    do {
        DEBUG(TEXT("[ListDirectory] Found file: %s"), File->Name);
        ListFile(File, Indent);
        if (Recurse && (File->Attributes & FS_ATTR_FOLDER)) {
            if (StringCompare(File->Name, TEXT(".")) != 0 && StringCompare(File->Name, TEXT("..")) != 0) {
                STR NewBase[MAX_PATH_NAME];
                StringCopy(NewBase, Base);
                if (NewBase[StringLength(NewBase) - 1] != PATH_SEP) StringConcat(NewBase, Sep);
                StringConcat(NewBase, File->Name);
                ListDirectory(Context, NewBase, Indent + 2, Pause, Recurse, NumListed);
            }
        }
        if (Pause) {
            (*NumListed)++;
            if (*NumListed >= Console.Height - 2) {
                *NumListed = 0;
                WaitKey();
            }
        }
    } while (FileSystem->Driver->Command(DF_FS_OPENNEXT, (UINT)File) == DF_ERROR_SUCCESS);

    FileSystem->Driver->Command(DF_FS_CLOSEFILE, (UINT)File);
}

/***************************************************************************/

static U32 CMD_commands(LPSHELLCONTEXT Context) {
    UNUSED(Context);

    U32 Index;

    for (Index = 0; COMMANDS[Index].Command != NULL; Index++) {
        ConsolePrint(TEXT("%s %s\n"), COMMANDS[Index].Name, COMMANDS[Index].Usage);
    }

    return DF_ERROR_SUCCESS;
}

/***************************************************************************/

static U32 CMD_cls(LPSHELLCONTEXT Context) {
    UNUSED(Context);

    ClearConsole();

    return DF_ERROR_SUCCESS;
}

/***************************************************************************/

static U32 CMD_dir(LPSHELLCONTEXT Context) {
    DEBUG(TEXT("[CMD_dir] Enter"));

    STR Target[MAX_PATH_NAME];
    STR Base[MAX_PATH_NAME];
    LPFILESYSTEM FileSystem = NULL;
    BOOL Pause;
    BOOL Recurse;
    U32 NumListed = 0;

    Target[0] = STR_NULL;

    // Parse all command line components (including options) first
    ParseNextCommandLineComponent(Context);
    if (StringLength(Context->Command)) {
        QualifyFileName(Context, Context->Command, Target);
    }

    // Continue parsing any remaining components to capture all options
    while (Context->Input.CommandLine[Context->CommandChar] != STR_NULL) {
        ParseNextCommandLineComponent(Context);
    }

    // Now check for options after all parsing is complete
    Pause = HasOption(Context, TEXT("p"), TEXT("pause"));
    Recurse = HasOption(Context, TEXT("r"), TEXT("recursive"));

    FileSystem = GetSystemFS();

    if (FileSystem == NULL || FileSystem->Driver == NULL) {
        ConsolePrint(TEXT("No file system mounted !\n"));
        return DF_ERROR_SUCCESS;
    }

    if (StringLength(Target) == 0) {
        StringCopy(Base, Context->CurrentFolder);
    } else {
        StringCopy(Base, Target);
    }

    ListDirectory(Context, Base, 0, Pause, Recurse, &NumListed);

    DEBUG(TEXT("[CMD_dir] Exit"));

    return DF_ERROR_SUCCESS;
}

/***************************************************************************/

static U32 CMD_cd(LPSHELLCONTEXT Context) {
    ChangeFolder(Context);
    return DF_ERROR_SUCCESS;
}

/***************************************************************************/

static U32 CMD_md(LPSHELLCONTEXT Context) {
    MakeFolder(Context);
    return DF_ERROR_SUCCESS;
}

/***************************************************************************/

/**
 * @brief Launch an executable specified on the command line.
 *
 * @param Context Shell context containing parsed arguments.
 */
static U32 CMD_run(LPSHELLCONTEXT Context) {
    BOOL Background = FALSE;

    ParseNextCommandLineComponent(Context);

    if (StringLength(Context->Command)) {
        Background = HasOption(Context, TEXT("-b"), TEXT("--background"));
        SpawnExecutable(Context, Context->Command, Background);
    }

    return DF_ERROR_SUCCESS;
}

/***************************************************************************/

static U32 CMD_exit(LPSHELLCONTEXT Context) {
    UNUSED(Context);
    return DF_ERROR_SUCCESS;
}

/***************************************************************************/

static U32 CMD_sysinfo(LPSHELLCONTEXT Context) {
    UNUSED(Context);

    SYSTEMINFO Info;

    Info.Header.Size = sizeof Info;
    Info.Header.Version = EXOS_ABI_VERSION;
    Info.Header.Flags = 0;
    DoSystemCall(SYSCALL_GetSystemInfo, (U32)&Info);

    ConsolePrint(TEXT("Total physical memory     : %u KB\n"), Info.TotalPhysicalMemory / N_1KB);
    ConsolePrint(TEXT("Physical memory used      : %u KB\n"), Info.PhysicalMemoryUsed / N_1KB);
    ConsolePrint(TEXT("Physical memory available : %u KB\n"), Info.PhysicalMemoryAvail / N_1KB);
    ConsolePrint(TEXT("Total swap memory         : %u KB\n"), Info.TotalSwapMemory / N_1KB);
    ConsolePrint(TEXT("Swap memory used          : %u KB\n"), Info.SwapMemoryUsed / N_1KB);
    ConsolePrint(TEXT("Swap memory available     : %u KB\n"), Info.SwapMemoryAvail / N_1KB);
    ConsolePrint(TEXT("Total memory available    : %u KB\n"), Info.TotalMemoryAvail / N_1KB);
    ConsolePrint(TEXT("Processor page size       : %u bytes\n"), Info.PageSize);
    ConsolePrint(TEXT("Total physical pages      : %u pages\n"), Info.TotalPhysicalPages);
    ConsolePrint(TEXT("Minimum linear address    : %x\n"), Info.MinimumLinearAddress);
    ConsolePrint(TEXT("Maximum linear address    : %x\n"), Info.MaximumLinearAddress);
    ConsolePrint(TEXT("User name                 : %s\n"), Info.UserName);
    ConsolePrint(TEXT("Number of processes       : %d\n"), Info.NumProcesses);
    ConsolePrint(TEXT("Number of tasks           : %d\n"), Info.NumTasks);
    ConsolePrint(TEXT("Keyboard layout           : %s\n"), Info.KeyboardLayout);

    return DF_ERROR_SUCCESS;
}

/***************************************************************************/

static U32 CMD_killtask(LPSHELLCONTEXT Context) {
    U32 TaskNum = 0;
    LPTASK Task = NULL;
    ParseNextCommandLineComponent(Context);
    TaskNum = StringToU32(Context->Command);
    Task = (LPTASK)ListGetItem(Kernel.Task, TaskNum);
    if (Task) KillTask(Task);

    return DF_ERROR_SUCCESS;
}

/***************************************************************************/

static U32 CMD_showprocess(LPSHELLCONTEXT Context) {
    LPPROCESS Process;
    ParseNextCommandLineComponent(Context);
    Process = ListGetItem(Kernel.Process, StringToU32(Context->Command));
    if (Process) DumpProcess(Process);

    return DF_ERROR_SUCCESS;
}

/***************************************************************************/

static U32 CMD_showtask(LPSHELLCONTEXT Context) {
    LPTASK Task;
    ParseNextCommandLineComponent(Context);
    Task = ListGetItem(Kernel.Task, StringToU32(Context->Command));

    if (Task) {
        DumpTask(Task);
    } else {
        STR Text[MAX_FILE_NAME];

        for (LPTASK Task = (LPTASK)Kernel.Task->First; Task != NULL; Task = (LPTASK)Task->Next) {
            StringPrintFormat(Text, TEXT("%x Status %x\n"), Task, Task->Status);
            ConsolePrint(Text);
        }
    }

    return DF_ERROR_SUCCESS;
}

/***************************************************************************/

static U32 CMD_memedit(LPSHELLCONTEXT Context) {
    ParseNextCommandLineComponent(Context);
    MemoryEditor(StringToU32(Context->Command));

    return DF_ERROR_SUCCESS;
}

/***************************************************************************/

static U32 CMD_disasm(LPSHELLCONTEXT Context) {
    DEBUG(TEXT("[CMD_disasm] Enter"));

    U32 Address = 0;
    U32 InstrCount = 0;
    STR Buffer[MAX_STRING_BUFFER];

    ParseNextCommandLineComponent(Context);
    Address = StringToU32(Context->Command);

    ParseNextCommandLineComponent(Context);
    InstrCount = StringToU32(Context->Command);

    if (Address != 0 && InstrCount > 0) {
        MemorySet(Buffer, 0, MAX_STRING_BUFFER);

        U32 NumBits = 32;
#if defined(__EXOS_ARCH_X86_64__)
        NumBits = 64;
#endif

        Disassemble(Buffer, Address, InstrCount, NumBits);
        ConsolePrint(Buffer);
    } else {
        ConsolePrint(TEXT("Missing parameter\n"));
    }

    DEBUG(TEXT("[CMD_disasm] Exit"));

    return DF_ERROR_SUCCESS;
}

/***************************************************************************/

static U32 CMD_cat(LPSHELLCONTEXT Context) {
    FILEOPENINFO FileOpenInfo;
    FILEOPERATION FileOperation;
    STR FileName[MAX_PATH_NAME];
    HANDLE Handle;
    U32 FileSize;
    U8* Buffer;

    ParseNextCommandLineComponent(Context);

    if (StringLength(Context->Command)) {
        if (QualifyFileName(Context, Context->Command, FileName)) {
            FileOpenInfo.Header.Size = sizeof(FILEOPENINFO);
            FileOpenInfo.Header.Version = EXOS_ABI_VERSION;
            FileOpenInfo.Header.Flags = 0;
            FileOpenInfo.Name = FileName;
            FileOpenInfo.Flags = FILE_OPEN_READ | FILE_OPEN_EXISTING;

            Handle = DoSystemCall(SYSCALL_OpenFile, (U32)&FileOpenInfo);

            if (Handle) {
                FileSize = DoSystemCall(SYSCALL_GetFileSize, Handle);

                if (FileSize) {
                    Buffer = (U8*)HeapAlloc(FileSize + 1);

                    if (Buffer) {
                        FileOperation.Header.Size = sizeof(FILEOPERATION);
                        FileOperation.Header.Version = EXOS_ABI_VERSION;
                        FileOperation.Header.Flags = 0;
                        FileOperation.File = Handle;
                        FileOperation.NumBytes = FileSize;
                        FileOperation.Buffer = Buffer;

                        if (DoSystemCall(SYSCALL_ReadFile, (U32)&FileOperation)) {
                            Buffer[FileSize] = STR_NULL;
                            ConsolePrint((LPSTR)Buffer);
                        }

                        HeapFree(Buffer);
                    }
                }
                DoSystemCall(SYSCALL_DeleteObject, Handle);
            }
        }
    }

    return DF_ERROR_SUCCESS;
}

/***************************************************************************/

static U32 CMD_copy(LPSHELLCONTEXT Context) {
    U8 Buffer[1024];
    FILEOPENINFO FileOpenInfo;
    FILEOPERATION FileOperation;
    STR SrcName[MAX_PATH_NAME];
    STR DstName[MAX_PATH_NAME];
    HANDLE SrcFile;
    HANDLE DstFile;
    U32 FileSize;
    U32 ByteCount;
    U32 Index;

    ParseNextCommandLineComponent(Context);
    if (QualifyFileName(Context, Context->Command, SrcName) == 0) return DF_ERROR_SUCCESS;

    ParseNextCommandLineComponent(Context);
    if (QualifyFileName(Context, Context->Command, DstName) == 0) return DF_ERROR_SUCCESS;

    ConsolePrint(TEXT("%s %s\n"), SrcName, DstName);

    FileOpenInfo.Header.Size = sizeof(FILEOPENINFO);
    FileOpenInfo.Header.Version = EXOS_ABI_VERSION;
    FileOpenInfo.Header.Flags = 0;
    FileOpenInfo.Name = SrcName;
    FileOpenInfo.Flags = FILE_OPEN_READ | FILE_OPEN_EXISTING;
    SrcFile = DoSystemCall(SYSCALL_OpenFile, (U32)&FileOpenInfo);
    if (SrcFile == NULL) return DF_ERROR_SUCCESS;

    FileOpenInfo.Header.Size = sizeof(FILEOPENINFO);
    FileOpenInfo.Header.Version = EXOS_ABI_VERSION;
    FileOpenInfo.Header.Flags = 0;
    FileOpenInfo.Name = DstName;
    FileOpenInfo.Flags = FILE_OPEN_WRITE;
    DstFile = DoSystemCall(SYSCALL_OpenFile, (U32)&FileOpenInfo);
    if (DstFile == NULL) {
        DoSystemCall(SYSCALL_DeleteObject, SrcFile);
        return DF_ERROR_SUCCESS;
    }

    FileSize = DoSystemCall(SYSCALL_GetFileSize, SrcFile);

    if (FileSize != 0) {
        for (Index = 0; Index < FileSize; Index += 1024) {
            ByteCount = 1024;
            if (Index + 1024 > FileSize) ByteCount = FileSize - Index;

            FileOperation.Header.Size = sizeof(FILEOPERATION);
            FileOperation.Header.Version = EXOS_ABI_VERSION;
            FileOperation.Header.Flags = 0;
            FileOperation.File = SrcFile;
            FileOperation.NumBytes = ByteCount;
            FileOperation.Buffer = Buffer;

            if (ReadFile(&FileOperation) != ByteCount) break;

            FileOperation.Header.Size = sizeof(FILEOPERATION);
            FileOperation.Header.Version = EXOS_ABI_VERSION;
            FileOperation.Header.Flags = 0;
            FileOperation.File = DstFile;
            FileOperation.NumBytes = ByteCount;
            FileOperation.Buffer = Buffer;

            if (WriteFile(&FileOperation) != ByteCount) break;
        }
    }

    DoSystemCall(SYSCALL_DeleteObject, SrcFile);
    DoSystemCall(SYSCALL_DeleteObject, DstFile);

    return DF_ERROR_SUCCESS;
}

/***************************************************************************/

static U32 CMD_edit(LPSHELLCONTEXT Context) {
    LPSTR Arguments[2];
    STR FileName[MAX_PATH_NAME];
    BOOL HasArgument = FALSE;
    BOOL ArgumentProvided = FALSE;
    BOOL LineNumbers;

    FileName[0] = STR_NULL;

    ParseNextCommandLineComponent(Context);

    if (StringLength(Context->Command)) {
        ArgumentProvided = TRUE;
        if (QualifyFileName(Context, Context->Command, FileName)) {
            Arguments[0] = FileName;
            HasArgument = TRUE;
        }
    }

    while (Context->Input.CommandLine[Context->CommandChar] != STR_NULL) {
        ParseNextCommandLineComponent(Context);
    }

    LineNumbers = HasOption(Context, TEXT("n"), TEXT("line-numbers"));

    if (HasArgument) {
        Edit(1, (LPCSTR*)Arguments, LineNumbers);
    } else if (!ArgumentProvided) {
        Edit(0, NULL, LineNumbers);
    }

    return DF_ERROR_SUCCESS;
}

/***************************************************************************/

static U32 CMD_hd(LPSHELLCONTEXT Context) {
    UNUSED(Context);

    LPLISTNODE Node;
    LPPHYSICALDISK Disk;
    DISKINFO DiskInfo;

    for (Node = Kernel.Disk->First; Node; Node = Node->Next) {
        Disk = (LPPHYSICALDISK)Node;

        DiskInfo.Disk = Disk;
        Disk->Driver->Command(DF_DISK_GETINFO, (UINT)&DiskInfo);

        ConsolePrint(TEXT("Manufacturer : %s\n"), Disk->Driver->Manufacturer);
        ConsolePrint(TEXT("Product      : %s\n"), Disk->Driver->Product);
        ConsolePrint(TEXT("Sectors      : %d\n"), DiskInfo.NumSectors);
        ConsolePrint(TEXT("\n"));
    }

    return DF_ERROR_SUCCESS;
}

/***************************************************************************/

static U32 CMD_filesystem(LPSHELLCONTEXT Context) {
    UNUSED(Context);

    LPLISTNODE Node;
    LPFILESYSTEM FileSystem;

    ConsolePrint(TEXT("General information\n"));

    if (StringEmpty(Kernel.FileSystemInfo.ActivePartitionName) == FALSE) {
        ConsolePrint(TEXT("Active partition : %s\n"), Kernel.FileSystemInfo.ActivePartitionName);
    } else {
        ConsolePrint(TEXT("Active partition : <none>\n"));
    }

    ConsolePrint(TEXT("\n"));
    ConsolePrint(TEXT("Mounted file systems\n"));

    for (Node = Kernel.FileSystem->First; Node; Node = Node->Next) {
        FileSystem = (LPFILESYSTEM)Node;

        ConsolePrint(TEXT("Name         : %s\n"), FileSystem->Name);
        ConsolePrint(TEXT("Manufacturer : %s\n"), FileSystem->Driver->Manufacturer);
        ConsolePrint(TEXT("Product      : %s\n"), FileSystem->Driver->Product);
        ConsolePrint(TEXT("\n"));
    }

    return DF_ERROR_SUCCESS;
}

/***************************************************************************/

static U32 CMD_network(LPSHELLCONTEXT Context) {
    UNUSED(Context);

    LPLISTNODE Node;

    SAFE_USE(Kernel.NetworkDevice) {
        for (Node = Kernel.NetworkDevice->First; Node; Node = Node->Next) {
            LPNETWORK_DEVICE_CONTEXT NetContext = (LPNETWORK_DEVICE_CONTEXT)Node;

            SAFE_USE_VALID_ID(NetContext, KOID_NETWORKDEVICE) {
                LPPCI_DEVICE Device = NetContext->Device;

                SAFE_USE_VALID_ID(Device, KOID_PCIDEVICE) {
                    SAFE_USE_VALID_ID(Device->Driver, KOID_DRIVER) {
                        // Get device info
                        NETWORKINFO Info;
                        MemorySet(&Info, 0, sizeof(Info));
                        NETWORKGETINFO GetInfo = {.Device = Device, .Info = &Info};
                        Device->Driver->Command(DF_NT_GETINFO, (UINT)(LPVOID)&GetInfo);

                        // Convert IP from network to host byte order
                        U32 IpHost = Ntohl(NetContext->LocalIPv4_Be);
                        U8 Ip1 = (IpHost >> 24) & 0xFF;
                        U8 Ip2 = (IpHost >> 16) & 0xFF;
                        U8 Ip3 = (IpHost >> 8) & 0xFF;
                        U8 Ip4 = IpHost & 0xFF;

                        ConsolePrint(TEXT("Name         : %s\n"), Device->Name);
                        ConsolePrint(TEXT("Manufacturer : %s\n"), Device->Driver->Manufacturer);
                        ConsolePrint(TEXT("Product      : %s\n"), Device->Driver->Product);
                        ConsolePrint(TEXT("MAC          : %x:%x:%x:%x:%x:%x\n"),
                                    Info.MAC[0], Info.MAC[1], Info.MAC[2],
                                    Info.MAC[3], Info.MAC[4], Info.MAC[5]);
                        ConsolePrint(TEXT("IP Address   : %u.%u.%u.%u\n"), Ip1, Ip2, Ip3, Ip4);
                        ConsolePrint(TEXT("Link         : %s\n"), Info.LinkUp ? TEXT("UP") : TEXT("DOWN"));
                        ConsolePrint(TEXT("Speed        : %u Mbps\n"), Info.SpeedMbps);
                        ConsolePrint(TEXT("Duplex       : %s\n"), Info.DuplexFull ? TEXT("FULL") : TEXT("HALF"));
                        ConsolePrint(TEXT("MTU          : %u\n"), Info.MTU);
                        ConsolePrint(TEXT("Initialized  : %s\n"), NetContext->IsInitialized ? TEXT("YES") : TEXT("NO"));
                        ConsolePrint(TEXT("\n"));
                    }
                }
            }
        }
    }

    return DF_ERROR_SUCCESS;
}

/***************************************************************************/

static U32 CMD_pic(LPSHELLCONTEXT Context) {
    UNUSED(Context);

    ConsolePrint(TEXT("8259-1 RM mask : %08b\n"), KernelStartup.IRQMask_21_RM);
    ConsolePrint(TEXT("8259-2 RM mask : %08b\n"), KernelStartup.IRQMask_A1_RM);
    ConsolePrint(TEXT("8259-1 PM mask : %08b\n"), KernelStartup.IRQMask_21_PM);
    ConsolePrint(TEXT("8259-2 PM mask : %08b\n"), KernelStartup.IRQMask_A1_PM);

    return DF_ERROR_SUCCESS;
}

/************************************************************************/

static U32 CMD_outp(LPSHELLCONTEXT Context) {
    U32 Port, Data;
    ParseNextCommandLineComponent(Context);
    Port = StringToU32(Context->Command);
    ParseNextCommandLineComponent(Context);
    Data = StringToU32(Context->Command);
    OutPortByte(Port, Data);

    return DF_ERROR_SUCCESS;
}

/************************************************************************/

static U32 CMD_inp(LPSHELLCONTEXT Context) {
    U32 Port, Data;
    ParseNextCommandLineComponent(Context);
    Port = StringToU32(Context->Command);
    Data = InPortByte(Port);
    ConsolePrint(TEXT("Port %X = %X\n"), Port, Data);

    return DF_ERROR_SUCCESS;
}

/************************************************************************/

static U32 CMD_reboot(LPSHELLCONTEXT Context) {
    UNUSED(Context);

    DEBUG(TEXT("[CMD_shutdown] Rebooting system"));
    ConsolePrint(TEXT("Rebooting system...\n"));

    ACPIReboot();

    return DF_ERROR_SUCCESS;
}

/************************************************************************/

/**
 * @brief Shutdown command implementation.
 * @param Context Shell context.
 */
static U32 CMD_shutdown(LPSHELLCONTEXT Context) {
    UNUSED(Context);

    DEBUG(TEXT("[CMD_shutdown] Shutting down system"));
    ConsolePrint(TEXT("Shutting down system...\n"));

    ACPIShutdown();

    return DF_ERROR_SUCCESS;
}

/************************************************************************/

static U32 CMD_adduser(LPSHELLCONTEXT Context) {
    STR UserName[MAX_USER_NAME];
    STR Password[MAX_USER_NAME];
    STR PrivilegeStr[16];
    U32 Privilege = EXOS_PRIVILEGE_ADMIN;  // Default to admin for first user

    DEBUG(TEXT("[CMD_adduser] Enter"));

    ParseNextCommandLineComponent(Context);
    if (StringLength(Context->Command) > 0) {
        StringCopy(UserName, Context->Command);
        DEBUG(TEXT("[CMD_adduser] Username from command: %s"), UserName);
    } else {
        ConsolePrint(TEXT("Enter username: "));
        ConsoleGetString(UserName, MAX_USER_NAME - 1);
        DEBUG(TEXT("[CMD_adduser] Username from input: %s"), UserName);
        if (StringLength(UserName) == 0) {
            ConsolePrint(TEXT("ERROR: Username cannot be empty\n"));
            return DF_ERROR_SUCCESS;
        }
    }

    ConsolePrint(TEXT("Password: "));
    CommandLineEditorReadLine(&Context->Input.Editor, Context->Input.CommandLine, sizeof Context->Input.CommandLine, TRUE);
    StringCopy(Password, Context->Input.CommandLine);

    DEBUG(TEXT("[CMD_adduser] Password entered (length: %d)"), StringLength(Password));

    // Check if this is the first user (no users exist yet)
    BOOL IsFirstUser = (Kernel.UserAccount == NULL || Kernel.UserAccount->First == NULL);
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

    DEBUG(TEXT("[CMD_adduser] Creating user with privilege %d"), Privilege);

    LPUSERACCOUNT Account = CreateUserAccount(UserName, Password, Privilege);

    SAFE_USE(Account) {
        ConsolePrint(TEXT("User created\n"));
    } else {
        ConsolePrint(TEXT("ERROR: Failed to create user '%s'\n"), UserName);
        DEBUG(TEXT("[CMD_adduser] CreateUserAccount returned NULL"));
    }

    DEBUG(TEXT("[CMD_adduser] Exit"));

    return DF_ERROR_SUCCESS;
}

/***************************************************************************/

static U32 CMD_deluser(LPSHELLCONTEXT Context) {
    STR UserName[MAX_USER_NAME];

    ParseNextCommandLineComponent(Context);
    if (StringLength(Context->Command) > 0) {
        StringCopy(UserName, Context->Command);
    } else {
        ConsolePrint(TEXT("Username to delete: "));
        ConsoleGetString(UserName, MAX_USER_NAME - 1);
        if (StringLength(UserName) == 0) {
            ConsolePrint(TEXT("Username cannot be empty\n"));
            return DF_ERROR_SUCCESS;
        }
    }

    LPUSERSESSION Session = GetCurrentSession();

    SAFE_USE(Session) {
        LPUSERACCOUNT CurrentAccount = FindUserAccountByID(Session->UserID);

        if (CurrentAccount == NULL || CurrentAccount->Privilege != EXOS_PRIVILEGE_ADMIN) {
            ConsolePrint(TEXT("Only admin users can delete accounts\n"));
            return DF_ERROR_SUCCESS;
        }
    }

    if (DeleteUserAccount(UserName)) {
        ConsolePrint(TEXT("User '%s' deleted successfully\n"), UserName);
        SaveUserDatabase();
    } else {
        ConsolePrint(TEXT("Failed to delete user '%s'\n"), UserName);
    }

    return DF_ERROR_SUCCESS;
}

/***************************************************************************/

static U32 CMD_login(LPSHELLCONTEXT Context) {
    STR UserName[MAX_USER_NAME];
    STR Password[MAX_USER_NAME];

    DEBUG(TEXT("[CMD_login] Enter"));

    ParseNextCommandLineComponent(Context);

    if (StringLength(Context->Command) > 0) {
        StringCopy(UserName, Context->Command);
        DEBUG(TEXT("[CMD_login] Username from command: %s"), UserName);
    } else {
        ConsolePrint(TEXT("Username: "));
        ConsoleGetString(UserName, MAX_USER_NAME - 1);

        DEBUG(TEXT("[CMD_login] Username from input: %s"), UserName);

        if (StringLength(UserName) == 0) {
            ConsolePrint(TEXT("ERROR: Username cannot be empty\n"));
            DEBUG(TEXT("[CMD_login] Empty username entered"));
            return DF_ERROR_SUCCESS;
        }
    }

    ConsolePrint(TEXT("Password: "));
    CommandLineEditorReadLine(&Context->Input.Editor, Context->Input.CommandLine, sizeof Context->Input.CommandLine, TRUE);
    StringCopy(Password, Context->Input.CommandLine);
    DEBUG(TEXT("[CMD_login] Password entered (length: %d)"), StringLength(Password));

    DEBUG(TEXT("[CMD_login] Looking for user '%s'"), UserName);
    LPUSERACCOUNT Account = FindUserAccount(UserName);
    if (Account == NULL) {
        ConsolePrint(TEXT("ERROR: User '%s' not found\n"), UserName);
        DEBUG(TEXT("[CMD_login] User not found"));
        return DF_ERROR_SUCCESS;
    }

    DEBUG(TEXT("[CMD_login] User found, verifying password"));
    if (!VerifyPassword(Password, Account->PasswordHash)) {
        ConsolePrint(TEXT("ERROR: Invalid password\n"));
        DEBUG(TEXT("[CMD_login] Password verification failed"));
        return DF_ERROR_SUCCESS;
    }

    DEBUG(TEXT("[CMD_login] Password verified, creating session"));
    LPUSERSESSION Session = CreateUserSession(Account->UserID, (HANDLE)GetCurrentTask());
    if (Session == NULL) {
        ConsolePrint(TEXT("ERROR: Failed to create session\n"));
        DEBUG(TEXT("[CMD_login] Session creation failed"));
        return DF_ERROR_SUCCESS;
    }

    DEBUG(TEXT("[CMD_login] Session created, updating login time"));
    GetLocalTime(&Account->LastLoginTime);

    DEBUG(TEXT("[CMD_login] Setting current session"));
    if (SetCurrentSession(Session)) {
        DEBUG(TEXT("[CMD_login] Session set successfully"));
    } else {
        ConsolePrint(TEXT("ERROR: Failed to set session\n"));
        DEBUG(TEXT("[CMD_login] Failed to set current session"));
        DestroyUserSession(Session);
    }

    DEBUG(TEXT("[CMD_login] Exit"));

    return DF_ERROR_SUCCESS;
}

/***************************************************************************/

static U32 CMD_logout(LPSHELLCONTEXT Context) {
    UNUSED(Context);

    LPUSERSESSION Session = GetCurrentSession();
    if (Session == NULL) {
        ConsolePrint(TEXT("No active session\n"));
        return DF_ERROR_SUCCESS;
    }

    DestroyUserSession(Session);
    SetCurrentSession(NULL);
    ConsolePrint(TEXT("Logged out successfully\n"));

    return DF_ERROR_SUCCESS;
}

/***************************************************************************/

static U32 CMD_whoami(LPSHELLCONTEXT Context) {
    UNUSED(Context);

    LPUSERSESSION Session = GetCurrentSession();
    if (Session == NULL) {
        ConsolePrint(TEXT("No active session\n"));
        return DF_ERROR_SUCCESS;
    }

    LPUSERACCOUNT Account = FindUserAccountByID(Session->UserID);
    if (Account == NULL) {
        ConsolePrint(TEXT("Session user not found\n"));
        return DF_ERROR_SUCCESS;
    }

    ConsolePrint(TEXT("Current user: %s\n"), Account->UserName);
    ConsolePrint(TEXT("Privilege: %s\n"), Account->Privilege == EXOS_PRIVILEGE_ADMIN ? TEXT("Admin") : TEXT("User"));
    ConsolePrint(
        TEXT("Login time: %d/%d/%d %d:%d:%d\n"), Session->LoginTime.Day, Session->LoginTime.Month,
        Session->LoginTime.Year, Session->LoginTime.Hour, Session->LoginTime.Minute, Session->LoginTime.Second);
    ConsolePrint(TEXT("Session ID: %lld\n"), Session->SessionID);

    return DF_ERROR_SUCCESS;
}

/***************************************************************************/

static U32 CMD_passwd(LPSHELLCONTEXT Context) {
    UNUSED(Context);
    STR OldPassword[MAX_PASSWORD];
    STR NewPassword[MAX_PASSWORD];
    STR ConfirmPassword[MAX_PASSWORD];

    LPUSERSESSION Session = GetCurrentSession();
    if (Session == NULL) {
        ConsolePrint(TEXT("No active session\n"));
        return DF_ERROR_SUCCESS;
    }

    LPUSERACCOUNT Account = FindUserAccountByID(Session->UserID);
    if (Account == NULL) {
        ConsolePrint(TEXT("Session user not found\n"));
        return DF_ERROR_SUCCESS;
    }

    ConsolePrint(TEXT("Password: "));
    CommandLineEditorReadLine(&Context->Input.Editor, Context->Input.CommandLine, sizeof Context->Input.CommandLine, TRUE);
    StringCopy(OldPassword, Context->Input.CommandLine);

    if (!VerifyPassword(OldPassword, Account->PasswordHash)) {
        ConsolePrint(TEXT("Invalid current password\n"));
        return DF_ERROR_SUCCESS;
    }

    ConsolePrint(TEXT("New password: "));
    CommandLineEditorReadLine(&Context->Input.Editor, Context->Input.CommandLine, sizeof Context->Input.CommandLine, TRUE);
    StringCopy(NewPassword, Context->Input.CommandLine);

    ConsolePrint(TEXT("Confirm password: "));
    CommandLineEditorReadLine(&Context->Input.Editor, Context->Input.CommandLine, sizeof Context->Input.CommandLine, TRUE);
    StringCopy(ConfirmPassword, Context->Input.CommandLine);

    if (StringCompare(NewPassword, ConfirmPassword) != 0) {
        ConsolePrint(TEXT("Passwords do not match\n"));
        return DF_ERROR_SUCCESS;
    }

    if (ChangeUserPassword(Account->UserName, OldPassword, NewPassword)) {
        ConsolePrint(TEXT("Password changed successfully\n"));
        SaveUserDatabase();
    } else {
        ConsolePrint(TEXT("Failed to change password\n"));
    }

    return DF_ERROR_SUCCESS;
}

/***************************************************************************/

/**
 * @brief Common function to spawn an executable.
 *
 * @param Context Shell context.
 * @param CommandName Name of the command/executable to spawn.
 * @param Background TRUE to run in background, FALSE for foreground.
 */
static BOOL SpawnExecutable(LPSHELLCONTEXT Context, LPCSTR CommandName, BOOL Background) {
    STR QualifiedCommandLine[MAX_PATH_NAME];

    if (QualifyCommandLine(Context, CommandName, QualifiedCommandLine)) {
        if (Background) {
            PROCESSINFO ProcessInfo;

            MemorySet(&ProcessInfo, 0, sizeof(ProcessInfo));

            ProcessInfo.Header.Size = sizeof(PROCESSINFO);
            ProcessInfo.Header.Version = EXOS_ABI_VERSION;
            ProcessInfo.Header.Flags = 0;
            ProcessInfo.Flags = 0;
            StringCopy(ProcessInfo.CommandLine, QualifiedCommandLine);
            StringCopy(ProcessInfo.WorkFolder, Context->CurrentFolder);
            ProcessInfo.StdOut = NULL;
            ProcessInfo.StdIn = NULL;
            ProcessInfo.StdErr = NULL;
            ProcessInfo.Process = NULL;
            ProcessInfo.Task = NULL;

            if (CreateProcess(&ProcessInfo)) {
                DEBUG(TEXT("Process started in background"));
            }
        } else {
            UINT ExitCode = Spawn(QualifiedCommandLine, Context->CurrentFolder);
            return (ExitCode != MAX_UINT);
        }
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
static void ExecuteStartupCommands(void) {
    U32 ConfigIndex = 0;
    STR Key[MAX_USER_NAME];
    LPCSTR CommandLine;
    SHELLCONTEXT Context;

    DEBUG(TEXT("[ExecuteStartupCommands] Enter"));

    // Wait 2 seconds for network stack to stabilize (ARP, etc.)
    DEBUG(TEXT("[ExecuteStartupCommands] Waiting 2000ms for network initialization"));
    Sleep(2000);
    DEBUG(TEXT("[ExecuteStartupCommands] Wait complete, starting commands"));

    LPTOML Configuration = GetConfiguration();
    if (Configuration == NULL) {
        DEBUG(TEXT("[ExecuteStartupCommands] Exit"));
        return;
    }

    InitShellContext(&Context);

    FOREVER {
        StringPrintFormat(Key, TEXT("Run.%u.Command"), ConfigIndex);
        CommandLine = TomlGet(Configuration, Key);
        if (CommandLine == NULL) break;

        DEBUG(TEXT("[ExecuteStartupCommands] Executing command: %s"), CommandLine);
        ExecuteCommandLine(&Context, CommandLine);

        ConfigIndex++;
    }

    DeinitShellContext(&Context);

    DEBUG(TEXT("[ExecuteStartupCommands] Exit"));
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
static void ExecuteCommandLine(LPSHELLCONTEXT Context, LPCSTR CommandLine) {
    SAFE_USE_3(Context, Context->ScriptContext, CommandLine) {
        DEBUG(TEXT("[ExecuteCommandLine] Executing: %s"), CommandLine);

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
static BOOL ParseCommand(LPSHELLCONTEXT Context) {
    DEBUG(TEXT("[ParseCommand] Enter"));

    ShowPrompt(Context);

    Context->Component = 0;
    Context->CommandChar = 0;
    MemorySet(Context->Input.CommandLine, 0, sizeof Context->Input.CommandLine);

    CommandLineEditorReadLine(&Context->Input.Editor, Context->Input.CommandLine, sizeof Context->Input.CommandLine, FALSE);

    if (Context->Input.CommandLine[0] != STR_NULL) {
        CommandLineEditorRemember(&Context->Input.Editor, Context->Input.CommandLine);
        ExecuteCommandLine(Context, Context->Input.CommandLine);
    }

    DEBUG(TEXT("[ParseCommand] Exit"));

    return TRUE;
}

/************************************************************************/

/**
 * @brief Shell callback for script output.
 * @param Message Message to output
 * @param UserData Shell context (unused)
 */
static void ShellScriptOutput(LPCSTR Message, LPVOID UserData) {
    UNUSED(UserData);
    DEBUG(TEXT("[ShellScriptOutput] %s"), Message);
    ConsolePrint(Message);
}

/************************************************************************/

/**
 * @brief Shell callback for script command execution.
 * @param Command Command to execute
 * @param UserData Shell context
 * @return DF_ERROR_SUCCESS on success or an error code on failure
 */
static U32 ShellScriptExecuteCommand(LPCSTR Command, LPVOID UserData) {
    LPSHELLCONTEXT Context = (LPSHELLCONTEXT)UserData;
    U32 Index;

    if (Context == NULL || Command == NULL) {
        return DF_ERROR_BADPARAM;
    }

    DEBUG(TEXT("[ShellScriptExecuteCommand] Executing: %s"), Command);

    StringCopy(Context->Input.CommandLine, Command);

    ClearOptions(Context);

    Context->Component = 0;
    Context->CommandChar = 0;

    ParseNextCommandLineComponent(Context);

    if (StringLength(Context->Command) == 0) {
        return DF_ERROR_SUCCESS;
    }

    {
        STR CommandName[MAX_FILE_NAME];
        StringCopy(CommandName, Context->Command);

        for (Index = 0; COMMANDS[Index].Command != NULL; Index++) {
            if (StringCompareNC(CommandName, COMMANDS[Index].Name) == 0 ||
                StringCompareNC(CommandName, COMMANDS[Index].AltName) == 0) {
                COMMANDS[Index].Command(Context);
                return DF_ERROR_SUCCESS;
            }
        }

        if (SpawnExecutable(Context, Context->Input.CommandLine, FALSE) == TRUE) {
            return DF_ERROR_SUCCESS;
        }

        if (Context->ScriptContext) {
            Context->ScriptContext->ErrorCode = SCRIPT_ERROR_SYNTAX;
            StringPrintFormat(
                Context->ScriptContext->ErrorMessage,
                TEXT("Unknown command: %s"),
                CommandName);
        }
    }

    return DF_ERROR_GENERIC;
}

/************************************************************************/

/**
 * @brief Shell callback for script variable resolution.
 * @param VarName Variable name to resolve
 * @param UserData Shell context (unused)
 * @return Variable value or NULL if not found
 */
static LPCSTR ShellScriptResolveVariable(LPCSTR VarName, LPVOID UserData) {
    UNUSED(UserData);
    DEBUG(TEXT("[ShellScriptResolveVariable] Resolving: %s"), VarName);
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
static U32 ShellScriptCallFunction(LPCSTR FuncName, LPCSTR Argument, LPVOID UserData) {
    LPSHELLCONTEXT Context = (LPSHELLCONTEXT)UserData;
    DEBUG(TEXT("[ShellScriptCallFunction] Calling: %s with arg: %s"), FuncName, Argument);

    if (STRINGS_EQUAL(FuncName, TEXT("exec"))) {
        if (Context == NULL || Argument == NULL) {
            DEBUG(TEXT("[ShellScriptCallFunction] Missing context or argument for exec"));
            return DF_ERROR_BADPARAM;
        }

        // Execute the provided command line using the standard shell command flow
        U32 Result = ShellScriptExecuteCommand(Argument, Context);
        DEBUG(TEXT("[ShellScriptCallFunction] exec returned: %u"), Result);
        return Result;
    } else if (STRINGS_EQUAL(FuncName, TEXT("print"))) {
        ConsolePrint(Argument);
        return 0;
    }

    DEBUG(TEXT("[ShellScriptCallFunction] Unknown function: %s"), FuncName);
    return MAX_U32;
}

/************************************************************************/

static BOOL HandleUserLoginProcess(void) {
    // Check if any users exist
    BOOL HasUsers = (Kernel.UserAccount != NULL && Kernel.UserAccount->First != NULL);

    if (!HasUsers) {
        // No users exist, prompt to create the first admin user
        ConsolePrint(TEXT("No existing user account. You need to create the first admin user.\n"));

        SHELLCONTEXT TempContext;
        InitShellContext(&TempContext);
        CMD_adduser(&TempContext);

        // Check if user was created successfully
        BOOL NewHasUsers = (Kernel.UserAccount != NULL && Kernel.UserAccount->First != NULL);

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

    DEBUG(TEXT("[Shell] Enter"));

    InitShellContext(&Context);

    if (Kernel.DoLogin && !HandleUserLoginProcess()) { return 0; }

    ExecuteStartupCommands();

    while (ParseCommand(&Context)) {
    }

    ConsolePrint(TEXT("Exiting shell\n"));

    DeinitShellContext(&Context);

    DEBUG(TEXT("[Shell] Exit"));

    TRACED_EPILOGUE("Shell");
    return 1;
}
