
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

#include "shell/Shell-Shared.h"

/************************************************************************/

static U32 CMD_commands(LPSHELLCONTEXT);
static U32 CMD_cls(LPSHELLCONTEXT);
static U32 CMD_conmode(LPSHELLCONTEXT);
static U32 CMD_keyboard(LPSHELLCONTEXT);
static U32 CMD_pause(LPSHELLCONTEXT);
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
static U32 CMD_memorymap(LPSHELLCONTEXT);
static U32 CMD_disk(LPSHELLCONTEXT);
static U32 CMD_filesystem(LPSHELLCONTEXT);
static U32 CMD_network(LPSHELLCONTEXT);
static U32 CMD_pic(LPSHELLCONTEXT);
static U32 CMD_outp(LPSHELLCONTEXT);
static U32 CMD_inp(LPSHELLCONTEXT);
static U32 CMD_reboot(LPSHELLCONTEXT);
static U32 CMD_shutdown(LPSHELLCONTEXT);
U32 CMD_adduser(LPSHELLCONTEXT);
static U32 CMD_deluser(LPSHELLCONTEXT);
U32 CMD_login(LPSHELLCONTEXT);
static U32 CMD_logout(LPSHELLCONTEXT);
static U32 CMD_whoami(LPSHELLCONTEXT);
static U32 CMD_passwd(LPSHELLCONTEXT);
static U32 CMD_prof(LPSHELLCONTEXT);
static U32 CMD_usb(LPSHELLCONTEXT);
static U32 CMD_nvme(LPSHELLCONTEXT);
static U32 CMD_dataview(LPSHELLCONTEXT);

static BOOL ShellCommandLineCompletion(
    const COMMANDLINE_COMPLETION_CONTEXT* CompletionContext,
    LPSTR Output,
    U32 OutputSize);
static void ShellRegisterScriptHostObjects(LPSHELLCONTEXT Context);
static void ListDirectory(LPSHELLCONTEXT, LPCSTR, U32, BOOL, BOOL, U32*);

/************************************************************************/
// The shell command table

SHELL_COMMAND_ENTRY COMMANDS[] = {
    {"commands", "help", "", CMD_commands},
    {"clear", "cls", "", CMD_cls},
    {"con_mode", "mode", "Columns Rows|list", CMD_conmode},
    {"keyboard", "keyboard", "--layout Code", CMD_keyboard},
    {"pause", "pause", "on|off", CMD_pause},
    {"ls", "dir", "[Name] [-p] [-r]", CMD_dir},
    {"cd", "cd", "Name", CMD_cd},
    {"mkdir", "md", "Name", CMD_md},
    {"run", "launch", "Name [-b|--background]", CMD_run},
    {"quit", "exit", "", CMD_exit},
    {"sys", "sys_info", "", CMD_sysinfo},
    {"kill", "kill_task", "Number", CMD_killtask},
    {"process", "show_process", "Number", CMD_showprocess},
    {"task", "show_task", "Number", CMD_showtask},
    {"mem", "mem_edit", "Address", CMD_memedit},
    {"dis", "disasm", "Address InstructionCount", CMD_disasm},
    {"memory_map", "memory_map", "", CMD_memorymap},
    {"cat", "type", "", CMD_cat},
    {"cp", "copy", "", CMD_copy},
    {"edit", "edit", "Name", CMD_edit},
    {"disk", "disk", "", CMD_disk},
    {"fs", "file_system", "[--long]", CMD_filesystem},
    {"net", "network", "devices", CMD_network},
    {"pic", "pic", "", CMD_pic},
    {"outp", "outp", "", CMD_outp},
    {"inp", "inp", "", CMD_inp},
    {"reboot", "reboot", "", CMD_reboot},
    {"shutdown", "power_off", "", CMD_shutdown},
    {"add_user", "new_user", "username", CMD_adduser},
    {"del_user", "delete_user", "username", CMD_deluser},
    {"login", "login", "", CMD_login},
    {"logout", "logout", "", CMD_logout},
    {"who_am_i", "who", "", CMD_whoami},
    {"passwd", "set_password", "", CMD_passwd},
    {"prof", "profiling", "", CMD_prof},
    {"usb", "usb", "ports|devices|tree|drives|probe", CMD_usb},
    {"nvme", "nvme", "list", CMD_nvme},
    {"data", "data_view", "", CMD_dataview},
    {"", "", "", NULL},
};

/************************************************************************/

static void ShellRegisterScriptHostObjects(LPSHELLCONTEXT Context) {

    if (Context == NULL || Context->ScriptContext == NULL) {
        return;
    }

    LPLIST ProcessList = GetProcessList();
    SAFE_USE(ProcessList) {
        if (!ScriptRegisterHostSymbol(
                Context->ScriptContext,
                TEXT("process"),
                SCRIPT_HOST_SYMBOL_ARRAY,
                ProcessList,
                &ProcessArrayDescriptor,
                NULL)) {
        }
    }

    LPLIST DriverList = GetDriverList();
    SAFE_USE(DriverList) {
        if (!ScriptRegisterHostSymbol(
                Context->ScriptContext,
                TEXT("drivers"),
                SCRIPT_HOST_SYMBOL_ARRAY,
                DriverList,
                &DriverArrayDescriptor,
                NULL)) {
        }
    }

    LPLIST StorageList = GetDiskList();
    SAFE_USE(StorageList) {
        if (!ScriptRegisterHostSymbol(
                Context->ScriptContext,
                TEXT("storage"),
                SCRIPT_HOST_SYMBOL_ARRAY,
                StorageList,
                &StorageArrayDescriptor,
                NULL)) {
        }
    }

    LPLIST PciDeviceList = GetPCIDeviceList();
    SAFE_USE(PciDeviceList) {
        if (!ScriptRegisterHostSymbol(
                Context->ScriptContext,
                TEXT("pci_bus"),
                SCRIPT_HOST_SYMBOL_ARRAY,
                PciDeviceList,
                &PciBusArrayDescriptor,
                NULL)) {
        }

        if (!ScriptRegisterHostSymbol(
                Context->ScriptContext,
                TEXT("pci_device"),
                SCRIPT_HOST_SYMBOL_ARRAY,
                PciDeviceList,
                &PciDeviceArrayDescriptor,
                NULL)) {
        }
    }

    if (!ScriptRegisterHostSymbol(
            Context->ScriptContext,
            TEXT("usb"),
            SCRIPT_HOST_SYMBOL_OBJECT,
            UsbRootHandle,
            &UsbDescriptor,
            NULL)) {
    }

    if (!ScriptRegisterHostSymbol(
            Context->ScriptContext,
            TEXT("keyboard"),
            SCRIPT_HOST_SYMBOL_OBJECT,
            GetKeyboardRootHandle(),
            GetKeyboardDescriptor(),
            NULL)) {
    }

    if (!ScriptRegisterHostSymbol(
            Context->ScriptContext,
            TEXT("mouse"),
            SCRIPT_HOST_SYMBOL_OBJECT,
            GetMouseRootHandle(),
            GetMouseDescriptor(),
            NULL)) {
    }
}

/************************************************************************/

void InitShellContext(LPSHELLCONTEXT This) {
    U32 Index;

    MemorySet(This, 0, sizeof(SHELLCONTEXT));


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

}

/***************************************************************************/

void DeinitShellContext(LPSHELLCONTEXT This) {
    U32 Index;


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

}

/***************************************************************************/

void ClearOptions(LPSHELLCONTEXT Context) {
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

BOOL ShowPrompt(LPSHELLCONTEXT Context) {
    ConsolePrint(TEXT("%s>"), Context->CurrentFolder);
    return TRUE;
}

/***************************************************************************/

BOOL ParseNextCommandLineComponent(LPSHELLCONTEXT Context) {
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

BOOL HasOption(LPSHELLCONTEXT Context, LPCSTR ShortName, LPCSTR LongName) {
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

BOOL QualifyFileName(LPSHELLCONTEXT Context, LPCSTR RawName, LPSTR FileName) {
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

BOOL QualifyCommandLine(LPSHELLCONTEXT Context, LPCSTR RawCommandLine, LPSTR QualifiedCommandLine) {
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
        FileInfo.Flags = 0;
        StringCopy(FileInfo.Name, FileName);
        FileSystem->Driver->Command(DF_FS_CREATEFOLDER, (UINT)&FileInfo);
    }
}

/***************************************************************************/

static void ListFile(LPFILE File, U32 Indent) {
    STR Name[MAX_FILE_NAME];
    U32 MaxWidth = Console.Width;
    U32 Length;
    U32 Index;


    //-------------------------------------
    // Eliminate the . and .. files

    if (StringCompare(File->Name, TEXT(".")) == 0) return;
    if (StringCompare(File->Name, TEXT("..")) == 0) return;


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
    FS_PATHCHECK PathCheck;
    STR DiskName[MAX_FILE_NAME];
    LPCSTR Reason = TEXT("unknown");
    STR Pattern[MAX_PATH_NAME];
    STR Sep[2] = {PATH_SEP, STR_NULL};

    UNUSED(Context);
    FileSystem = GetSystemFS();

    Find.Size = sizeof(FILEINFO);
    Find.FileSystem = FileSystem;
    Find.Attributes = MAX_U32;
    Find.Flags = FILE_OPEN_READ | FILE_OPEN_EXISTING;

    StringCopy(Pattern, Base);
    if (Pattern[StringLength(Pattern) - 1] != PATH_SEP) StringConcat(Pattern, Sep);
    StringConcat(Pattern, TEXT("*"));
    StringCopy(Find.Name, Pattern);

    File = (LPFILE)FileSystem->Driver->Command(DF_FS_OPENFILE, (UINT)&Find);
    if (File == NULL) {
        StringCopy(Find.Name, Base);
        File = (LPFILE)FileSystem->Driver->Command(DF_FS_OPENFILE, (UINT)&Find);
        if (File == NULL) {
            StringCopy(DiskName, Base);
            if (Base[0] == PATH_SEP && Base[1] == 'f' && Base[2] == 's' && Base[3] == PATH_SEP) {
                UINT ReadIndex = 4;
                UINT WriteIndex = 0;
                while (Base[ReadIndex] != STR_NULL && Base[ReadIndex] != PATH_SEP && WriteIndex < MAX_FILE_NAME - 1) {
                    DiskName[WriteIndex++] = Base[ReadIndex++];
                }
                DiskName[WriteIndex] = STR_NULL;
            }

            PathCheck.CurrentFolder[0] = STR_NULL;
            StringCopy(PathCheck.SubFolder, Base);
            if (FileSystem->Driver->Command(DF_FS_PATHEXISTS, (UINT)&PathCheck)) {
                Reason = TEXT("file system driver refused open/list");
            } else {
                Reason = TEXT("path not found");
            }
            ConsolePrint(TEXT("Unable to read on volume %s, reason : %s\n"), DiskName, Reason);
            WARNING(TEXT("[ListDirectory] Unable to read on volume %s, reason : %s (path=%s fs=%s driver=%s)"),
                DiskName,
                Reason,
                Base,
                FileSystem->Name,
                FileSystem->Driver->Product);
            return;
        }
        ListFile(File, Indent);
        FileSystem->Driver->Command(DF_FS_CLOSEFILE, (UINT)File);
        return;
    }

    do {
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
    } while (FileSystem->Driver->Command(DF_FS_OPENNEXT, (UINT)File) == DF_RETURN_SUCCESS);

    FileSystem->Driver->Command(DF_FS_CLOSEFILE, (UINT)File);
}

/***************************************************************************/

static U32 CMD_commands(LPSHELLCONTEXT Context) {
    UNUSED(Context);

    U32 Index;

    for (Index = 0; COMMANDS[Index].Command != NULL; Index++) {
        ConsolePrint(TEXT("%s %s\n"), COMMANDS[Index].Name, COMMANDS[Index].Usage);
    }

    return DF_RETURN_SUCCESS;
}

/***************************************************************************/

static U32 CMD_cls(LPSHELLCONTEXT Context) {
    UNUSED(Context);

    ClearConsole();

    return DF_RETURN_SUCCESS;
}

/***************************************************************************/

static U32 CMD_conmode(LPSHELLCONTEXT Context) {
    GRAPHICSMODEINFO Info;
    U32 Columns;
    U32 Rows;
    U32 Result;
    U32 ModeCount;

    ParseNextCommandLineComponent(Context);
    if (StringLength(Context->Command) == 0) {
        ConsolePrint(TEXT("Usage: con_mode Columns Rows | con_mode list\n"));
        return DF_RETURN_SUCCESS;
    }

    if (StringCompareNC(Context->Command, TEXT("list")) == 0) {
        CONSOLEMODEINFO ModeInfo;
        ModeCount = DoSystemCall(SYSCALL_ConsoleGetModeCount, SYSCALL_PARAM(0));
        ConsolePrint(TEXT("VGA text modes:\n"));
        for (U32 Index = 0; Index < ModeCount; Index++) {
            ModeInfo.Header.Size = sizeof ModeInfo;
            ModeInfo.Header.Version = EXOS_ABI_VERSION;
            ModeInfo.Header.Flags = 0;
            ModeInfo.Index = Index;
            if (DoSystemCall(SYSCALL_ConsoleGetModeInfo, SYSCALL_PARAM(&ModeInfo)) != DF_RETURN_SUCCESS) {
                continue;
            }
            ConsolePrint(TEXT("  %u: %ux%u (char height %u)\n"),
                Index,
                ModeInfo.Columns,
                ModeInfo.Rows,
                ModeInfo.CharHeight);
        }
        return DF_RETURN_SUCCESS;
    }

    Columns = StringToU32(Context->Command);

    ParseNextCommandLineComponent(Context);
    if (StringLength(Context->Command) == 0) {
        ConsolePrint(TEXT("Usage: con_mode Columns Rows | con_mode list\n"));
        return DF_RETURN_SUCCESS;
    }
    Rows = StringToU32(Context->Command);

    if (Columns == 0 || Rows == 0) {
        ConsolePrint(TEXT("Invalid console size\n"));
        return DF_RETURN_SUCCESS;
    }

    Info.Header.Size = sizeof Info;
    Info.Header.Version = EXOS_ABI_VERSION;
    Info.Header.Flags = 0;
    Info.Width = Columns;
    Info.Height = Rows;
    Info.BitsPerPixel = 0;

    Result = DoSystemCall(SYSCALL_ConsoleSetMode, SYSCALL_PARAM(&Info));

    if (Result != DF_RETURN_SUCCESS) {
        ConsolePrint(TEXT("Console mode %ux%u unavailable (err=%u)\n"), Columns, Rows, Result);
    } else {
        ConsolePrint(TEXT("Console mode set to %ux%u\n"), Columns, Rows);
    }

    return DF_RETURN_SUCCESS;
}

/***************************************************************************/

/**
 * @brief Update or display the active keyboard layout.
 * @param Context Shell context.
 * @return DF_RETURN_SUCCESS.
 */
static U32 CMD_keyboard(LPSHELLCONTEXT Context) {
    ParseNextCommandLineComponent(Context);

    if (StringLength(Context->Command) == 0) {
        ConsolePrint(TEXT("Keyboard layout: %s\n"), GetKeyboardCode());
        return DF_RETURN_SUCCESS;
    }

    if (HasOption(Context, "l", "layout")) {
        SelectKeyboard(Context->Command);
        ConsolePrint(TEXT("Keyboard layout set to %s\n"), GetKeyboardCode());
        TEST(TEXT("[CMD_keyboard] keyboard : OK"));
        return DF_RETURN_SUCCESS;
    }

    ConsolePrint(TEXT("Usage: keyboard --layout Code\n"));
    return DF_RETURN_SUCCESS;
}

/***************************************************************************/

static U32 CMD_pause(LPSHELLCONTEXT Context) {
    ParseNextCommandLineComponent(Context);

    if (StringLength(Context->Command) == 0) {
        ConsolePrint(TEXT("Pause is %s\n"), ConsoleGetPagingEnabled() ? TEXT("on") : TEXT("off"));
        return DF_RETURN_SUCCESS;
    }

    if (StringCompareNC(Context->Command, TEXT("on")) == 0) {
        ConsoleSetPagingEnabled(TRUE);
        ConsolePrint(TEXT("Pause on\n"));
        return DF_RETURN_SUCCESS;
    }

    if (StringCompareNC(Context->Command, TEXT("off")) == 0) {
        ConsoleSetPagingEnabled(FALSE);
        ConsolePrint(TEXT("Pause off\n"));
        return DF_RETURN_SUCCESS;
    }

    ConsolePrint(TEXT("Usage: pause on|off\n"));
    return DF_RETURN_SUCCESS;
}

/***************************************************************************/

static U32 CMD_dir(LPSHELLCONTEXT Context) {

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
        TEST(TEXT("[CMD_dir] dir : KO (No file system mounted)"));
        return DF_RETURN_SUCCESS;
    }

    if (StringLength(Target) == 0) {
        StringCopy(Base, Context->CurrentFolder);
    } else {
        StringCopy(Base, Target);
    }

    ListDirectory(Context, Base, 0, Pause, Recurse, &NumListed);

    TEST(TEXT("[CMD_dir] dir : OK"));

    return DF_RETURN_SUCCESS;
}

/***************************************************************************/

static U32 CMD_cd(LPSHELLCONTEXT Context) {
    ChangeFolder(Context);
    return DF_RETURN_SUCCESS;
}

/***************************************************************************/

static U32 CMD_md(LPSHELLCONTEXT Context) {
    MakeFolder(Context);
    return DF_RETURN_SUCCESS;
}

/***************************************************************************/

/**
 * @brief Load and execute an E0 script file.
 *
 * @param Context Shell context containing the script interpreter instance.
 * @param ScriptFileName Qualified script path.
 * @return TRUE on successful execution, FALSE otherwise.
 */
BOOL RunScriptFile(LPSHELLCONTEXT Context, LPCSTR ScriptFileName) {
    FILEOPENINFO FileOpenInfo;
    FILEOPERATION FileOperation;
    HANDLE Handle = NULL;
    U32 FileSize = 0;
    U32 BytesRead = 0;
    U8* Buffer = NULL;
    STR ReturnText[64];
    SCRIPT_VAR_TYPE ReturnType;
    SCRIPT_VAR_VALUE ReturnValue;
    SCRIPT_ERROR Error = SCRIPT_OK;
    BOOL Success = FALSE;

    if (Context == NULL || ScriptFileName == NULL || Context->ScriptContext == NULL) {
        return FALSE;
    }

    FileOpenInfo.Header.Size = sizeof(FILEOPENINFO);
    FileOpenInfo.Header.Version = EXOS_ABI_VERSION;
    FileOpenInfo.Header.Flags = 0;
    FileOpenInfo.Name = ScriptFileName;
    FileOpenInfo.Flags = FILE_OPEN_READ | FILE_OPEN_EXISTING;

    Handle = DoSystemCall(SYSCALL_OpenFile, SYSCALL_PARAM(&FileOpenInfo));

    if (Handle == NULL) {
        ConsolePrint(TEXT("Unable to open script file: %s\n"), ScriptFileName);
        goto Out;
    }

    FileSize = DoSystemCall(SYSCALL_GetFileSize, SYSCALL_PARAM(Handle));
    if (FileSize == 0) {
        ConsolePrint(TEXT("Empty script file: %s\n"), ScriptFileName);
        goto Out;
    }

    Buffer = (U8*)HeapAlloc(FileSize + 1);
    if (Buffer == NULL) {
        ConsolePrint(TEXT("Unable to allocate script buffer: %u bytes\n"), FileSize + 1);
        goto Out;
    }

    FileOperation.Header.Size = sizeof(FILEOPERATION);
    FileOperation.Header.Version = EXOS_ABI_VERSION;
    FileOperation.Header.Flags = 0;
    FileOperation.File = Handle;
    FileOperation.NumBytes = FileSize;
    FileOperation.Buffer = Buffer;

    BytesRead = DoSystemCall(SYSCALL_ReadFile, SYSCALL_PARAM(&FileOperation));
    if (BytesRead != FileSize) {
        ConsolePrint(TEXT("Failed to read script file: %s\n"), ScriptFileName);
        goto Out;
    }

    Buffer[FileSize] = STR_NULL;

    Error = ScriptExecute(Context->ScriptContext, (LPCSTR)Buffer);
    if (Error != SCRIPT_OK) {
        ConsolePrint(TEXT("Error: %s\n"), ScriptGetErrorMessage(Context->ScriptContext));
        goto Out;
    }

    if (ScriptGetReturnValue(Context->ScriptContext, &ReturnType, &ReturnValue)) {
        if (ReturnType == SCRIPT_VAR_STRING) {
            StringCopy(ReturnText, ReturnValue.String ? ReturnValue.String : TEXT(""));
        } else if (ReturnType == SCRIPT_VAR_INTEGER) {
            StringPrintFormat(ReturnText, TEXT("%d"), ReturnValue.Integer);
        } else if (ReturnType == SCRIPT_VAR_FLOAT) {
            StringPrintFormat(ReturnText, TEXT("%f"), ReturnValue.Float);
        } else {
            StringCopy(ReturnText, TEXT("unsupported"));
        }

        ConsolePrint(TEXT("Script return value: %s\n"), ReturnText);
        TEST(TEXT("[CMD_script] Script return value: %s"), ReturnText);
    }

    Success = TRUE;

Out:
    if (Buffer != NULL) {
        HeapFree(Buffer);
    }

    if (Handle != NULL) {
        DoSystemCall(SYSCALL_DeleteObject, SYSCALL_PARAM(Handle));
    }

    return Success;
}

/***************************************************************************/

/**
 * @brief Launch an executable specified on the command line.
 *
 * @param Context Shell context containing parsed arguments.
 */
static U32 CMD_run(LPSHELLCONTEXT Context) {
    STR TargetName[MAX_PATH_NAME];
    BOOL Background = FALSE;

    ParseNextCommandLineComponent(Context);

    if (StringLength(Context->Command)) {
        StringCopy(TargetName, Context->Command);

        while (Context->Input.CommandLine[Context->CommandChar] != STR_NULL) {
            ParseNextCommandLineComponent(Context);
        }

        Background = HasOption(Context, TEXT("b"), TEXT("background"));
        SpawnExecutable(Context, TargetName, Background);
    }

    return DF_RETURN_SUCCESS;
}

/***************************************************************************/

static U32 CMD_exit(LPSHELLCONTEXT Context) {
    UNUSED(Context);
    return DF_RETURN_SUCCESS;
}

/***************************************************************************/

/**
 * @brief Convert a byte count to kilobytes for console display.
 * @param Value Byte count in 64-bit representation.
 * @return Kilobytes clipped to UINT range.
 */
static UINT BytesToKiloBytesForDisplay(U64 Value) {
#ifdef __EXOS_32__
    U64 Shifted = Value;

    for (UINT Index = 0; Index < 10; Index++) {
        Shifted = U64_ShiftRight1(Shifted);
    }

    return U64_ToU32_Clip(Shifted);
#else
    return (UINT)(Value >> 10);
#endif
}

/***************************************************************************/

static U32 CMD_sysinfo(LPSHELLCONTEXT Context) {
    UNUSED(Context);

    SYSTEMINFO Info;

    Info.Header.Size = sizeof Info;
    Info.Header.Version = EXOS_ABI_VERSION;
    Info.Header.Flags = 0;
    DoSystemCall(SYSCALL_GetSystemInfo, SYSCALL_PARAM(&Info));

    ConsolePrint(TEXT("Total physical memory     : %u KB\n"), BytesToKiloBytesForDisplay(Info.TotalPhysicalMemory));
    ConsolePrint(TEXT("Physical memory used      : %u KB\n"), BytesToKiloBytesForDisplay(Info.PhysicalMemoryUsed));
    ConsolePrint(TEXT("Physical memory available : %u KB\n"), BytesToKiloBytesForDisplay(Info.PhysicalMemoryAvail));
    ConsolePrint(TEXT("Total swap memory         : %u KB\n"), BytesToKiloBytesForDisplay(Info.TotalSwapMemory));
    ConsolePrint(TEXT("Swap memory used          : %u KB\n"), BytesToKiloBytesForDisplay(Info.SwapMemoryUsed));
    ConsolePrint(TEXT("Swap memory available     : %u KB\n"), BytesToKiloBytesForDisplay(Info.SwapMemoryAvail));
    ConsolePrint(TEXT("Total memory available    : %u KB\n"), BytesToKiloBytesForDisplay(Info.TotalMemoryAvail));
    ConsolePrint(TEXT("Processor page size       : %u bytes\n"), Info.PageSize);
    ConsolePrint(TEXT("Total physical pages      : %u pages\n"), Info.TotalPhysicalPages);
    ConsolePrint(TEXT("Minimum linear address    : %x\n"), Info.MinimumLinearAddress);
    ConsolePrint(TEXT("Maximum linear address    : %x\n"), Info.MaximumLinearAddress);
    ConsolePrint(TEXT("User name                 : %s\n"), Info.UserName);
    ConsolePrint(TEXT("Number of processes       : %d\n"), Info.NumProcesses);
    ConsolePrint(TEXT("Number of tasks           : %d\n"), Info.NumTasks);
    ConsolePrint(TEXT("Keyboard layout           : %s\n"), Info.KeyboardLayout);

    TEST(TEXT("[CMD_sysinfo] sys_info : OK"));
    return DF_RETURN_SUCCESS;
}

/***************************************************************************/

static U32 CMD_killtask(LPSHELLCONTEXT Context) {
    U32 TaskNum = 0;
    LPTASK Task = NULL;
    ParseNextCommandLineComponent(Context);
    TaskNum = StringToU32(Context->Command);
    LPLIST TaskList = GetTaskList();
    Task = (LPTASK)ListGetItem(TaskList, TaskNum);
    if (Task) KillTask(Task);

    return DF_RETURN_SUCCESS;
}

/***************************************************************************/

static U32 CMD_showprocess(LPSHELLCONTEXT Context) {
    LPPROCESS Process;
    ParseNextCommandLineComponent(Context);
    LPLIST ProcessList = GetProcessList();
    Process = ListGetItem(ProcessList, StringToU32(Context->Command));
    if (Process) DumpProcess(Process);

    return DF_RETURN_SUCCESS;
}

/***************************************************************************/

static U32 CMD_showtask(LPSHELLCONTEXT Context) {
    LPTASK Task;
    ParseNextCommandLineComponent(Context);
    LPLIST TaskList = GetTaskList();
    Task = ListGetItem(TaskList, StringToU32(Context->Command));

    if (Task) {
        DumpTask(Task);
    } else {
        STR Text[MAX_FILE_NAME];

        for (LPTASK Task = (LPTASK)TaskList->First; Task != NULL; Task = (LPTASK)Task->Next) {
            StringPrintFormat(Text, TEXT("%x Status %x\n"), Task, Task->Status);
            ConsolePrint(Text);
        }
    }

    return DF_RETURN_SUCCESS;
}

/***************************************************************************/

static U32 CMD_memedit(LPSHELLCONTEXT Context) {
    ParseNextCommandLineComponent(Context);
    MemoryEditor(StringToU32(Context->Command));

    return DF_RETURN_SUCCESS;
}

/***************************************************************************/

static U32 CMD_memorymap(LPSHELLCONTEXT Context) {
    UNUSED(Context);

    LPPROCESS Process = &KernelProcess;
    LPMEMORY_REGION_DESCRIPTOR Descriptor = Process->RegionListHead;
    UINT Index = 0;

    ConsolePrint(TEXT("Kernel regions: %u\n"), Process->RegionCount);

    while (Descriptor != NULL) {
        LPCSTR Tag = (Descriptor->Tag[0] == STR_NULL) ? TEXT("???") : Descriptor->Tag;
        if (Descriptor->PhysicalBase == 0) {
            ConsolePrint(TEXT("%u: tag=%s base=%p size=%u phys=???\n"),
                Index,
                Tag,
                (LPVOID)Descriptor->CanonicalBase,
                Descriptor->Size);
        } else {
            ConsolePrint(TEXT("%u: tag=%s base=%p size=%u phys=%p\n"),
                Index,
                Tag,
                (LPVOID)Descriptor->CanonicalBase,
                Descriptor->Size,
                (LPVOID)Descriptor->PhysicalBase);
        }
        Descriptor = (LPMEMORY_REGION_DESCRIPTOR)Descriptor->Next;
        Index++;
    }

    return DF_RETURN_SUCCESS;
}

/***************************************************************************/

static U32 CMD_disasm(LPSHELLCONTEXT Context) {

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


    return DF_RETURN_SUCCESS;
}

/***************************************************************************/

static U32 CMD_cat(LPSHELLCONTEXT Context) {
    FILEOPENINFO FileOpenInfo;
    FILEOPERATION FileOperation;
    STR FileName[MAX_PATH_NAME];
    HANDLE Handle;
    U32 FileSize;
    U8* Buffer;
    BOOL Success = FALSE;

    ParseNextCommandLineComponent(Context);

    if (StringLength(Context->Command)) {
        if (QualifyFileName(Context, Context->Command, FileName)) {
            FileOpenInfo.Header.Size = sizeof(FILEOPENINFO);
            FileOpenInfo.Header.Version = EXOS_ABI_VERSION;
            FileOpenInfo.Header.Flags = 0;
            FileOpenInfo.Name = FileName;
            FileOpenInfo.Flags = FILE_OPEN_READ | FILE_OPEN_EXISTING;

            Handle = DoSystemCall(SYSCALL_OpenFile, SYSCALL_PARAM(&FileOpenInfo));

            if (Handle) {
                FileSize = DoSystemCall(SYSCALL_GetFileSize, SYSCALL_PARAM(Handle));

                if (FileSize) {
                    Buffer = (U8*)HeapAlloc(FileSize + 1);

                    if (Buffer) {
                        FileOperation.Header.Size = sizeof(FILEOPERATION);
                        FileOperation.Header.Version = EXOS_ABI_VERSION;
                        FileOperation.Header.Flags = 0;
                        FileOperation.File = Handle;
                        FileOperation.NumBytes = FileSize;
                        FileOperation.Buffer = Buffer;

                        if (DoSystemCall(SYSCALL_ReadFile, SYSCALL_PARAM(&FileOperation))) {
                            Buffer[FileSize] = STR_NULL;
                            ConsolePrint((LPSTR)Buffer);
                            Success = TRUE;
                        }

                        HeapFree(Buffer);
                    }
                }
                DoSystemCall(SYSCALL_DeleteObject, SYSCALL_PARAM(Handle));
            }
        }
    }

    if (Success) {
        TEST(TEXT("[CMD_type] type %s : OK"), FileName);
    } else {
        TEST(TEXT("[CMD_type] type : KO"));
    }

    return DF_RETURN_SUCCESS;
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
    U32 TotalCopied = 0;
    BOOL Success = FALSE;

    ParseNextCommandLineComponent(Context);
    if (QualifyFileName(Context, Context->Command, SrcName) == 0) return DF_RETURN_SUCCESS;

    ParseNextCommandLineComponent(Context);
    if (QualifyFileName(Context, Context->Command, DstName) == 0) return DF_RETURN_SUCCESS;

    ConsolePrint(TEXT("%s %s\n"), SrcName, DstName);

    FileOpenInfo.Header.Size = sizeof(FILEOPENINFO);
    FileOpenInfo.Header.Version = EXOS_ABI_VERSION;
    FileOpenInfo.Header.Flags = 0;
    FileOpenInfo.Name = SrcName;
    FileOpenInfo.Flags = FILE_OPEN_READ | FILE_OPEN_EXISTING;
    SrcFile = DoSystemCall(SYSCALL_OpenFile, SYSCALL_PARAM(&FileOpenInfo));
    if (SrcFile == NULL) {
        TEST(TEXT("[CMD_copy] copy %s %s : KO"), SrcName, DstName);
        return DF_RETURN_SUCCESS;
    }

    FileOpenInfo.Header.Size = sizeof(FILEOPENINFO);
    FileOpenInfo.Header.Version = EXOS_ABI_VERSION;
    FileOpenInfo.Header.Flags = 0;
    FileOpenInfo.Name = DstName;
    FileOpenInfo.Flags = FILE_OPEN_WRITE | FILE_OPEN_CREATE_ALWAYS | FILE_OPEN_TRUNCATE;
    DstFile = DoSystemCall(SYSCALL_OpenFile, SYSCALL_PARAM(&FileOpenInfo));
    if (DstFile == NULL) {
        DoSystemCall(SYSCALL_DeleteObject, SYSCALL_PARAM(SrcFile));
        TEST(TEXT("[CMD_copy] copy %s %s : KO"), SrcName, DstName);
        return DF_RETURN_SUCCESS;
    }

    FileSize = DoSystemCall(SYSCALL_GetFileSize, SYSCALL_PARAM(SrcFile));
    if (FileSize != 0) {
        for (Index = 0; Index < FileSize; Index += 1024) {
            U32 ReadResult;
            U32 WriteResult;
            ByteCount = 1024;
            if (Index + 1024 > FileSize) ByteCount = FileSize - Index;

            FileOperation.Header.Size = sizeof(FILEOPERATION);
            FileOperation.Header.Version = EXOS_ABI_VERSION;
            FileOperation.Header.Flags = 0;
            FileOperation.File = SrcFile;
            FileOperation.NumBytes = ByteCount;
            FileOperation.Buffer = Buffer;

            ReadResult = DoSystemCall(SYSCALL_ReadFile, SYSCALL_PARAM(&FileOperation));
            if (ReadResult != ByteCount) {
                DEBUG(TEXT("[CMD_copy] Read failed at %u (expected %u got %u)"), Index, ByteCount, ReadResult);
                break;
            }

            FileOperation.Header.Size = sizeof(FILEOPERATION);
            FileOperation.Header.Version = EXOS_ABI_VERSION;
            FileOperation.Header.Flags = 0;
            FileOperation.File = DstFile;
            FileOperation.NumBytes = ByteCount;
            FileOperation.Buffer = Buffer;

            WriteResult = DoSystemCall(SYSCALL_WriteFile, SYSCALL_PARAM(&FileOperation));
            if (WriteResult != ByteCount) {
                DEBUG(TEXT("[CMD_copy] Write failed at %u (expected %u got %u)"), Index, ByteCount, WriteResult);
                break;
            }
            TotalCopied += ByteCount;
        }
    }

    Success = (TotalCopied == FileSize);
    DEBUG(TEXT("[CMD_copy] TotalCopied=%u FileSize=%u"), TotalCopied, FileSize);

    DoSystemCall(SYSCALL_DeleteObject, SYSCALL_PARAM(SrcFile));
    DoSystemCall(SYSCALL_DeleteObject, SYSCALL_PARAM(DstFile));

    if (Success) {
        TEST(TEXT("[CMD_copy] copy %s %s : OK"), SrcName, DstName);
    } else {
        TEST(TEXT("[CMD_copy] copy %s %s : KO"), SrcName, DstName);
    }

    return DF_RETURN_SUCCESS;
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

    LineNumbers = HasOption(Context, TEXT("n"), TEXT("line_numbers"));

    if (HasArgument) {
        Edit(1, (LPCSTR*)Arguments, LineNumbers);
    } else if (!ArgumentProvided) {
        Edit(0, NULL, LineNumbers);
    }

    return DF_RETURN_SUCCESS;
}

/***************************************************************************/

static U32 CMD_disk(LPSHELLCONTEXT Context) {
    UNUSED(Context);

    LPLISTNODE Node;
    LPSTORAGE_UNIT Disk;
    DISKINFO DiskInfo;

    LPLIST DiskList = GetDiskList();
    for (Node = DiskList != NULL ? DiskList->First : NULL; Node; Node = Node->Next) {
        Disk = (LPSTORAGE_UNIT)Node;

        DiskInfo.Disk = Disk;
        Disk->Driver->Command(DF_DISK_GETINFO, (UINT)&DiskInfo);

        ConsolePrint(TEXT("Manufacturer : %s\n"), Disk->Driver->Manufacturer);
        ConsolePrint(TEXT("Product      : %s\n"), Disk->Driver->Product);
        ConsolePrint(TEXT("Sector size  : %u\n"), DiskInfo.BytesPerSector);
        ConsolePrint(TEXT("Sectors      : %x%08x\n"),
                     (U32)U64_High32(DiskInfo.NumSectors),
                     (U32)U64_Low32(DiskInfo.NumSectors));
        ConsolePrint(TEXT("\n"));
    }

    return DF_RETURN_SUCCESS;
}

/***************************************************************************/

static U32 CMD_filesystem(LPSHELLCONTEXT Context) {
    LPLISTNODE Node;
    LPFILESYSTEM FileSystem;
    BOOL LongMode;

    ParseNextCommandLineComponent(Context);
    LongMode = HasOption(Context, TEXT("l"), TEXT("long"));

    if (StringLength(Context->Command) != 0) {
        ConsolePrint(TEXT("Usage: fs [--long]\n"));
        return DF_RETURN_SUCCESS;
    }

    if (LongMode) {
        ConsolePrint(TEXT("General information\n"));
        FILESYSTEM_GLOBAL_INFO* FileSystemInfo = GetFileSystemGlobalInfo();

        if (StringEmpty(FileSystemInfo->ActivePartitionName) == FALSE) {
            ConsolePrint(TEXT("Active partition : %s\n"), FileSystemInfo->ActivePartitionName);
        } else {
            ConsolePrint(TEXT("Active partition : <none>\n"));
        }

        ConsolePrint(TEXT("\n"));
        ConsolePrint(TEXT("Discovered file systems\n"));
    } else {
        ConsolePrint(TEXT("%-12s %-12s %-10s %11s\n"),
            TEXT("Name"), TEXT("Type"), TEXT("Format"), TEXT("Size"));
        ConsolePrint(TEXT("-------------------------------------------------\n"));
    }

    U32 UnmountedCount = 0;
    LPLIST Lists[2] = {GetFileSystemList(), GetUnusedFileSystemList()};
    for (U32 ListIndex = 0; ListIndex < 2; ListIndex++) {
        LPLIST FileSystemList = Lists[ListIndex];
        for (Node = FileSystemList != NULL ? FileSystemList->First : NULL; Node; Node = Node->Next) {
            DISKINFO DiskInfo;
            BOOL DiskInfoValid = FALSE;
            LPSTORAGE_UNIT StorageUnit;
            U32 PartitionSizeMiB;

            FileSystem = (LPFILESYSTEM)Node;
            StorageUnit = FileSystemGetStorageUnit(FileSystem);
            PartitionSizeMiB = FileSystem->Partition.NumSectors / 2048;

            if (FileSystem->Mounted == FALSE) {
                UnmountedCount++;
            }

            if (!LongMode) {
                STR DisplayName[MAX_FS_LOGICAL_NAME + 2];
                StringCopy(DisplayName, FileSystem->Name);
                if (FileSystem->Mounted == FALSE) {
                    StringConcat(DisplayName, TEXT("*"));
                }

                ConsolePrint(TEXT("%-12s %-12s %-10s %7u MiB\n"),
                    DisplayName,
                    FileSystemGetPartitionTypeName(&FileSystem->Partition),
                    FileSystemGetPartitionFormatName(FileSystem->Partition.Format),
                    PartitionSizeMiB);
                continue;
            }

            ConsolePrint(TEXT("Name         : %s\n"), FileSystem->Name);
            ConsolePrint(TEXT("Mounted      : %s\n"), FileSystem->Mounted ? TEXT("YES") : TEXT("NO"));
            if (FileSystem->Driver != NULL) {
                ConsolePrint(TEXT("FS driver    : %s / %s\n"), FileSystem->Driver->Manufacturer, FileSystem->Driver->Product);
            } else {
                ConsolePrint(TEXT("FS driver    : <none>\n"));
            }
            ConsolePrint(TEXT("Scheme       : %s\n"), FileSystemGetPartitionSchemeName(FileSystem->Partition.Scheme));
            ConsolePrint(TEXT("Type         : %s\n"), FileSystemGetPartitionTypeName(&FileSystem->Partition));
            ConsolePrint(TEXT("Format       : %s\n"), FileSystemGetPartitionFormatName(FileSystem->Partition.Format));
            if (FileSystem->Partition.Format == PARTITION_FORMAT_NTFS) {
                NTFS_VOLUME_GEOMETRY Geometry;
                MemorySet(&Geometry, 0, sizeof(NTFS_VOLUME_GEOMETRY));
                if (NtfsGetVolumeGeometry(FileSystem, &Geometry)) {
                    ConsolePrint(TEXT("NTFS bytes/sector   : %u\n"), Geometry.BytesPerSector);
                    ConsolePrint(TEXT("NTFS sectors/cluster: %u\n"), Geometry.SectorsPerCluster);
                    ConsolePrint(TEXT("NTFS bytes/cluster  : %u\n"), Geometry.BytesPerCluster);
                    ConsolePrint(TEXT("NTFS record size    : %u\n"), Geometry.FileRecordSize);
                    ConsolePrint(TEXT("NTFS MFT LCN : %x, %x\n"),
                        (U32)U64_High32(Geometry.MftStartCluster),
                        (U32)U64_Low32(Geometry.MftStartCluster));
                    if (StringEmpty(Geometry.VolumeLabel)) {
                        ConsolePrint(TEXT("NTFS label   : <unknown>\n"));
                    } else {
                        ConsolePrint(TEXT("NTFS label   : %s\n"), Geometry.VolumeLabel);
                    }
                }
            }
            ConsolePrint(TEXT("Index        : %u\n"), FileSystem->Partition.Index);
            ConsolePrint(TEXT("Start sector : %u\n"), FileSystem->Partition.StartSector);
            ConsolePrint(TEXT("Size         : %u sectors (%u MiB)\n"),
                FileSystem->Partition.NumSectors, PartitionSizeMiB);
            ConsolePrint(TEXT("Active       : %s\n"),
                (FileSystem->Partition.Flags & PARTITION_FLAG_ACTIVE) ? TEXT("YES") : TEXT("NO"));

            if (FileSystem->Partition.Scheme == PARTITION_SCHEME_MBR) {
                ConsolePrint(TEXT("Type id      : %x\n"), FileSystem->Partition.Type);
            } else if (FileSystem->Partition.Scheme == PARTITION_SCHEME_GPT) {
                ConsolePrint(TEXT("Type GUID    : %x-%x-%x-%x-%x-%x-%x-%x-%x-%x-%x-%x-%x-%x-%x-%x\n"),
                    FileSystem->Partition.TypeGuid[0], FileSystem->Partition.TypeGuid[1],
                    FileSystem->Partition.TypeGuid[2], FileSystem->Partition.TypeGuid[3],
                    FileSystem->Partition.TypeGuid[4], FileSystem->Partition.TypeGuid[5],
                    FileSystem->Partition.TypeGuid[6], FileSystem->Partition.TypeGuid[7],
                    FileSystem->Partition.TypeGuid[8], FileSystem->Partition.TypeGuid[9],
                    FileSystem->Partition.TypeGuid[10], FileSystem->Partition.TypeGuid[11],
                    FileSystem->Partition.TypeGuid[12], FileSystem->Partition.TypeGuid[13],
                    FileSystem->Partition.TypeGuid[14], FileSystem->Partition.TypeGuid[15]);
            }

            if (StorageUnit != NULL && StorageUnit->Driver != NULL) {
                MemorySet(&DiskInfo, 0, sizeof(DISKINFO));
                DiskInfo.Disk = StorageUnit;
                if (StorageUnit->Driver->Command(DF_DISK_GETINFO, (UINT)&DiskInfo) == DF_RETURN_SUCCESS) {
                    DiskInfoValid = TRUE;
                }
                ConsolePrint(TEXT("Storage      : %s / %s\n"),
                    StorageUnit->Driver->Manufacturer, StorageUnit->Driver->Product);
            } else {
                ConsolePrint(TEXT("Storage      : <none>\n"));
            }

            if (DiskInfoValid) {
                ConsolePrint(TEXT("Removable    : %s\n"), DiskInfo.Removable ? TEXT("YES") : TEXT("NO"));
                ConsolePrint(TEXT("Read only    : %s\n"),
                    (DiskInfo.Access & DISK_ACCESS_READONLY) ? TEXT("YES") : TEXT("NO"));
                ConsolePrint(TEXT("Disk sectors : %x, %x\n"),
                    (U32)U64_High32(DiskInfo.NumSectors),
                    (U32)U64_Low32(DiskInfo.NumSectors));
            }
            ConsolePrint(TEXT("\n"));
        }
    }

    if (!LongMode && UnmountedCount > 0) {
        ConsolePrint(TEXT("\n"));
        ConsolePrint(TEXT("* = unmounted\n"));
    }

    return DF_RETURN_SUCCESS;
}

/***************************************************************************/

static U32 CMD_network(LPSHELLCONTEXT Context) {
    ParseNextCommandLineComponent(Context);

    if (StringLength(Context->Command) == 0 ||
        StringCompareNC(Context->Command, TEXT("devices")) != 0) {
        ConsolePrint(TEXT("Usage: network devices\n"));
        return DF_RETURN_SUCCESS;
    }

    LPLIST NetworkDeviceList = GetNetworkDeviceList();
    if (NetworkDeviceList == NULL || NetworkDeviceList->First == NULL) {
        ConsolePrint(TEXT("No network device detected\n"));
        return DF_RETURN_SUCCESS;
    }

    SAFE_USE(NetworkDeviceList) {
        for (LPLISTNODE Node = NetworkDeviceList->First; Node; Node = Node->Next) {
            LPNETWORK_DEVICE_CONTEXT NetContext = (LPNETWORK_DEVICE_CONTEXT)Node;

            SAFE_USE_VALID_ID(NetContext, KOID_NETWORKDEVICE) {
                LPPCI_DEVICE Device = NetContext->Device;

                SAFE_USE_VALID_ID(Device, KOID_PCIDEVICE) {
                    SAFE_USE_VALID_ID(Device->Driver, KOID_DRIVER) {
                        NETWORKINFO Info;
                        MemorySet(&Info, 0, sizeof(Info));
                        NETWORKGETINFO GetInfo = {.Device = Device, .Info = &Info};
                        Device->Driver->Command(DF_NT_GETINFO, (UINT)(LPVOID)&GetInfo);

                        U32 IpHost = Ntohl(NetContext->ActiveConfig.LocalIPv4_Be);
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

    return DF_RETURN_SUCCESS;
}

/***************************************************************************/

static U32 CMD_pic(LPSHELLCONTEXT Context) {
    UNUSED(Context);

    ConsolePrint(TEXT("8259-1 RM mask : %08b\n"), KernelStartup.IRQMask_21_RM);
    ConsolePrint(TEXT("8259-2 RM mask : %08b\n"), KernelStartup.IRQMask_A1_RM);
    ConsolePrint(TEXT("8259-1 PM mask : %08b\n"), KernelStartup.IRQMask_21_PM);
    ConsolePrint(TEXT("8259-2 PM mask : %08b\n"), KernelStartup.IRQMask_A1_PM);

    return DF_RETURN_SUCCESS;
}

/************************************************************************/

static U32 CMD_outp(LPSHELLCONTEXT Context) {
    U32 Port, Data;
    ParseNextCommandLineComponent(Context);
    Port = StringToU32(Context->Command);
    ParseNextCommandLineComponent(Context);
    Data = StringToU32(Context->Command);
    OutPortByte(Port, Data);

    return DF_RETURN_SUCCESS;
}

/************************************************************************/

static U32 CMD_inp(LPSHELLCONTEXT Context) {
    U32 Port, Data;
    ParseNextCommandLineComponent(Context);
    Port = StringToU32(Context->Command);
    Data = InPortByte(Port);
    ConsolePrint(TEXT("Port %X = %X\n"), Port, Data);

    return DF_RETURN_SUCCESS;
}

/************************************************************************/

static U32 CMD_reboot(LPSHELLCONTEXT Context) {
    UNUSED(Context);

    ConsolePrint(TEXT("Rebooting system...\n"));

    RebootKernel();

    return DF_RETURN_SUCCESS;
}

/************************************************************************/

/**
 * @brief Shutdown command implementation.
 * @param Context Shell context.
 */
static U32 CMD_shutdown(LPSHELLCONTEXT Context) {
    UNUSED(Context);

    ConsolePrint(TEXT("Shutting down system...\n"));

    ShutdownKernel();

    return DF_RETURN_SUCCESS;
}

/************************************************************************/

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

static U32 CMD_logout(LPSHELLCONTEXT Context) {
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

static U32 CMD_whoami(LPSHELLCONTEXT Context) {
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

static U32 CMD_passwd(LPSHELLCONTEXT Context) {
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

static U32 CMD_prof(LPSHELLCONTEXT Context) {
    UNUSED(Context);
    ProfileDump();
    return 0;
}

/***************************************************************************/

/**
 * @brief Run the System Data View mode from the shell.
 * @param Context Shell context.
 * @return DF_RETURN_SUCCESS on completion.
 */
static U32 CMD_dataview(LPSHELLCONTEXT Context) {
    UNUSED(Context);
    SystemDataViewMode();
    return DF_RETURN_SUCCESS;
}

/***************************************************************************/

/**
 * @brief USB control command (xHCI port report).
 * @param Context Shell context.
 * @return DF_RETURN_SUCCESS on completion.
 */
static U32 CMD_usb(LPSHELLCONTEXT Context) {
    ParseNextCommandLineComponent(Context);

    if (StringLength(Context->Command) == 0 ||
        (StringCompareNC(Context->Command, TEXT("ports")) != 0 &&
         StringCompareNC(Context->Command, TEXT("devices")) != 0 &&
         StringCompareNC(Context->Command, TEXT("device-tree")) != 0 &&
         StringCompareNC(Context->Command, TEXT("drives")) != 0 &&
         StringCompareNC(Context->Command, TEXT("probe")) != 0)) {
        ConsolePrint(TEXT("Usage: usb ports|devices|device-tree|drives|probe\n"));
        return DF_RETURN_SUCCESS;
    }

    if (StringCompareNC(Context->Command, TEXT("drives")) == 0) {
        LPLIST UsbStorageList = GetUsbStorageList();
        if (UsbStorageList == NULL || UsbStorageList->First == NULL) {
            ConsolePrint(TEXT("No USB drive detected\n"));
            return DF_RETURN_SUCCESS;
        }

        UINT Index = 0;
        for (LPLISTNODE Node = UsbStorageList->First; Node; Node = Node->Next) {
            LPUSB_STORAGE_ENTRY Entry = (LPUSB_STORAGE_ENTRY)Node;
            if (Entry == NULL) {
                continue;
            }

            ConsolePrint(TEXT("usb%u: addr=%x vid=%x pid=%x blocks=%u block_size=%u state=%s\n"),
                         Index,
                         (U32)Entry->Address,
                         (U32)Entry->VendorId,
                         (U32)Entry->ProductId,
                         Entry->BlockCount,
                         Entry->BlockSize,
                         Entry->Present ? TEXT("online") : TEXT("offline"));
            Index++;
        }

        return DF_RETURN_SUCCESS;
    }

    DRIVER_ENUM_QUERY Query;
    MemorySet(&Query, 0, sizeof(Query));
    Query.Header.Size = sizeof(Query);
    Query.Header.Version = EXOS_ABI_VERSION;
    if (StringCompareNC(Context->Command, TEXT("probe")) == 0) {
        DRIVER_ENUM_QUERY PortQuery;
        MemorySet(&PortQuery, 0, sizeof(PortQuery));
        PortQuery.Header.Size = sizeof(PortQuery);
        PortQuery.Header.Version = EXOS_ABI_VERSION;
        PortQuery.Domain = ENUM_DOMAIN_XHCI_PORT;
        PortQuery.Flags = 0;

        UINT ProviderIndexProbe = 0;
        DRIVER_ENUM_PROVIDER ProviderProbe = NULL;
        BOOL FoundProbe = FALSE;

        while (KernelEnumGetProvider(&PortQuery, ProviderIndexProbe, &ProviderProbe) == DF_RETURN_SUCCESS) {
            DRIVER_ENUM_ITEM ItemProbe;
            PortQuery.Index = 0;
            FoundProbe = TRUE;

            MemorySet(&ItemProbe, 0, sizeof(ItemProbe));
            ItemProbe.Header.Size = sizeof(ItemProbe);
            ItemProbe.Header.Version = EXOS_ABI_VERSION;

            while (KernelEnumNext(ProviderProbe, &PortQuery, &ItemProbe) == DF_RETURN_SUCCESS) {
                const DRIVER_ENUM_XHCI_PORT* Data = (const DRIVER_ENUM_XHCI_PORT*)ItemProbe.Data;
                if (ItemProbe.DataSize < sizeof(DRIVER_ENUM_XHCI_PORT)) {
                    break;
                }
                if (Data->Connected) {
                    if (Data->LastEnumError == XHCI_ENUM_ERROR_ENABLE_SLOT) {
                        ConsolePrint(TEXT("P%u Err=%s C=%u\n"),
                                     (U32)Data->PortNumber,
                                     UsbEnumErrorToString(Data->LastEnumError),
                                     (U32)Data->LastEnumCompletion);
                    } else {
                        ConsolePrint(TEXT("P%u Err=%s\n"),
                                     (U32)Data->PortNumber,
                                     UsbEnumErrorToString(Data->LastEnumError));
                    }
                }
            }
            ProviderIndexProbe++;
        }

        if (!FoundProbe) {
            ConsolePrint(TEXT("No xHCI controller detected\n"));
        }
        return DF_RETURN_SUCCESS;
    } else if (StringCompareNC(Context->Command, TEXT("devices")) == 0) {
        Query.Domain = ENUM_DOMAIN_USB_DEVICE;
    } else if (StringCompareNC(Context->Command, TEXT("device-tree")) == 0) {
        Query.Domain = ENUM_DOMAIN_USB_NODE;
    } else {
        Query.Domain = ENUM_DOMAIN_XHCI_PORT;
    }
    Query.Flags = 0;

    UINT ProviderIndex = 0;
    BOOL Found = FALSE;
    BOOL Printed = FALSE;
    DRIVER_ENUM_PROVIDER Provider = NULL;

    while (KernelEnumGetProvider(&Query, ProviderIndex, &Provider) == DF_RETURN_SUCCESS) {
        DRIVER_ENUM_ITEM Item;
        STR Buffer[256];

        Found = TRUE;
        Query.Index = 0;

        MemorySet(&Item, 0, sizeof(Item));
        Item.Header.Size = sizeof(Item);
        Item.Header.Version = EXOS_ABI_VERSION;

        while (KernelEnumNext(Provider, &Query, &Item) == DF_RETURN_SUCCESS) {
            if (KernelEnumPretty(Provider, &Query, &Item, Buffer, sizeof(Buffer)) == DF_RETURN_SUCCESS) {
                ConsolePrint(TEXT("%s\n"), Buffer);
                Printed = TRUE;
            }
        }

        ProviderIndex++;
    }

    if (!Found) {
        ConsolePrint(TEXT("No xHCI controller detected\n"));
        return DF_RETURN_SUCCESS;
    }

    if (!Printed && Query.Domain == ENUM_DOMAIN_USB_DEVICE) {
        ConsolePrint(TEXT("No USB device detected\n"));
    } else if (!Printed && Query.Domain == ENUM_DOMAIN_USB_NODE) {
        ConsolePrint(TEXT("No USB device tree detected\n"));
    }
    return DF_RETURN_SUCCESS;
}

/***************************************************************************/

/**
 * @brief NVMe control command (device list).
 * @param Context Shell context.
 * @return DF_RETURN_SUCCESS on completion.
 */
static U32 CMD_nvme(LPSHELLCONTEXT Context) {
    ParseNextCommandLineComponent(Context);

    if (StringLength(Context->Command) == 0 ||
        StringCompareNC(Context->Command, TEXT("list")) != 0) {
        ConsolePrint(TEXT("Usage: nvme list\n"));
        return DF_RETURN_SUCCESS;
    }

    DRIVER_ENUM_QUERY Query;
    MemorySet(&Query, 0, sizeof(Query));
    Query.Header.Size = sizeof(Query);
    Query.Header.Version = EXOS_ABI_VERSION;
    Query.Domain = ENUM_DOMAIN_PCI_DEVICE;
    Query.Flags = 0;

    UINT ProviderIndex = 0;
    BOOL Found = FALSE;
    BOOL Printed = FALSE;
    DRIVER_ENUM_PROVIDER Provider = NULL;
    UINT Index = 0;

    while (KernelEnumGetProvider(&Query, ProviderIndex, &Provider) == DF_RETURN_SUCCESS) {
        DRIVER_ENUM_ITEM Item;

        Found = TRUE;
        Query.Index = 0;

        MemorySet(&Item, 0, sizeof(Item));
        Item.Header.Size = sizeof(Item);
        Item.Header.Version = EXOS_ABI_VERSION;

        while (KernelEnumNext(Provider, &Query, &Item) == DF_RETURN_SUCCESS) {
            if (Item.DataSize < sizeof(DRIVER_ENUM_PCI_DEVICE)) {
                break;
            }

            const DRIVER_ENUM_PCI_DEVICE* Data = (const DRIVER_ENUM_PCI_DEVICE*)Item.Data;
            if (Data->BaseClass != NVME_PCI_CLASS ||
                Data->SubClass != NVME_PCI_SUBCLASS ||
                Data->ProgIF != NVME_PCI_PROG_IF) {
                continue;
            }

            ConsolePrint(TEXT("nvme%u: bus=%x device=%x function=%x vendor_identifier=%x device_identifier=%x revision=%x\n"),
                         Index,
                         (U32)Data->Bus,
                         (U32)Data->Dev,
                         (U32)Data->Func,
                         (U32)Data->VendorID,
                         (U32)Data->DeviceID,
                         (U32)Data->Revision);
            Index++;
            Printed = TRUE;
        }

        ProviderIndex++;
    }

    if (!Found) {
        ConsolePrint(TEXT("No PCI device provider detected\n"));
        return DF_RETURN_SUCCESS;
    }

    if (!Printed) {
        ConsolePrint(TEXT("No NVMe device detected\n"));
    }

    return DF_RETURN_SUCCESS;
}

/***************************************************************************/

/**
 * @brief Common function to launch an executable or an E0 script.
 *
 * @param Context Shell context.
 * @param CommandName Name of the command/executable to spawn.
 * @param Background TRUE to run in background, FALSE for foreground.
 */
BOOL SpawnExecutable(LPSHELLCONTEXT Context, LPCSTR CommandName, BOOL Background) {
    STR QualifiedCommandLine[MAX_PATH_NAME];
    STR QualifiedCommand[MAX_PATH_NAME];
    U32 CommandIndex = 0;

    if (QualifyCommandLine(Context, CommandName, QualifiedCommandLine)) {
        while (QualifiedCommandLine[CommandIndex] != STR_NULL &&
               QualifiedCommandLine[CommandIndex] > STR_SPACE &&
               CommandIndex < MAX_PATH_NAME - 1) {
            QualifiedCommand[CommandIndex] = QualifiedCommandLine[CommandIndex];
            CommandIndex++;
        }
        QualifiedCommand[CommandIndex] = STR_NULL;

        if (ScriptIsE0FileName(QualifiedCommand)) {
            if (Background) {
                ConsolePrint(TEXT("E0 scripts cannot be started in background mode.\n"));
                return FALSE;
            }
            return RunScriptFile(Context, QualifiedCommand);
        }

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
            }
        } else {
            UINT ExitCode = Spawn(QualifiedCommandLine, Context->CurrentFolder);
            return (ExitCode != MAX_UINT);
        }
    }

    return FALSE;
}

/***************************************************************************/
