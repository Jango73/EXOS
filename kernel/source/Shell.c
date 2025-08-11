
/***************************************************************************\

    EXOS Kernel
    Copyright (c) 1999-2025 Jango73
    All rights reserved

\***************************************************************************/

#include "../include/Base.h"
#include "../include/Console.h"
#include "../include/File.h"
#include "../include/FileSys.h"
#include "../include/GFX.h"
#include "../include/HD.h"
#include "../include/Heap.h"
#include "../include/Kernel.h"
#include "../include/Keyboard.h"
#include "../include/List.h"
#include "../include/Log.h"
#include "../include/String.h"
#include "../include/System.h"
#include "../include/User.h"

/***************************************************************************/

#define NUM_BUFFERS 8
#define BUFFER_SIZE 1024

/***************************************************************************/

typedef struct tag_SHELLCONTEXT {
    U32 Component;
    U32 CommandChar;
    STR CommandLine[BUFFER_SIZE];
    STR Command[256];
    STR CurrentVolume[MAX_FS_LOGICAL_NAME];
    STR CurrentFolder[MAX_PATH_NAME];
    LPVOID BufferBase;
    U32 BufferSize;
    LPSTR Buffer[NUM_BUFFERS];
} SHELLCONTEXT, *LPSHELLCONTEXT;

/***************************************************************************/

typedef void (*SHELLCOMMAND)(LPSHELLCONTEXT);

static void CMD_commands(LPSHELLCONTEXT);
static void CMD_cls(LPSHELLCONTEXT);
static void CMD_dir(LPSHELLCONTEXT);
static void CMD_cd(LPSHELLCONTEXT);
static void CMD_md(LPSHELLCONTEXT);
static void CMD_run(LPSHELLCONTEXT);
static void CMD_exit(LPSHELLCONTEXT);
static void CMD_sysinfo(LPSHELLCONTEXT);
static void CMD_killtask(LPSHELLCONTEXT);
static void CMD_showprocess(LPSHELLCONTEXT);
static void CMD_showtask(LPSHELLCONTEXT);
static void CMD_memedit(LPSHELLCONTEXT);
static void CMD_cat(LPSHELLCONTEXT);
static void CMD_copy(LPSHELLCONTEXT);
static void CMD_edit(LPSHELLCONTEXT);
static void CMD_hd(LPSHELLCONTEXT);
static void CMD_filesystem(LPSHELLCONTEXT);
static void CMD_irq(LPSHELLCONTEXT);
static void CMD_outp(LPSHELLCONTEXT);
static void CMD_inp(LPSHELLCONTEXT);
static void CMD_reboot(LPSHELLCONTEXT);
static void CMD_dos(LPSHELLCONTEXT);

static struct {
    STR Name[32];
    STR AltName[32];
    STR Usage[32];
    SHELLCOMMAND Command;
} COMMANDS[] = {
    {"commands", "help", "", CMD_commands},
    {"clear", "cls", "", CMD_cls},
    {"ls", "dir", "[Name] [/P]", CMD_dir},
    {"cd", "cd", "Name", CMD_cd},
    {"mkdir", "md", "Name", CMD_md},
    {"run", "launch", "", CMD_run},
    {"quit", "exit", "", CMD_exit},
    {"sys", "sysinfo", "", CMD_sysinfo},
    {"kill", "killtask", "", CMD_killtask},
    {"process", "showprocess", "", CMD_showprocess},
    {"task", "showtask", "", CMD_showtask},
    {"mem", "memedit", "", CMD_memedit},
    {"cat", "type", "", CMD_cat},
    {"cp", "copy", "", CMD_copy},
    {"edit", "edit", "Name", CMD_edit},
    {"hd", "hd", "", CMD_hd},
    {"fs", "filesystem", "", CMD_filesystem},
    {"irq", "irq", "", CMD_irq},
    {"outp", "outp", "", CMD_outp},
    {"inp", "inp", "", CMD_inp},
    {"reboot", "reboot", "", CMD_reboot},
    {"dos", "dos", "", CMD_dos},
    {"", "", "", NULL},
};

/***************************************************************************/

static void InitShellContext(LPSHELLCONTEXT This) {
    U32 Index;

    KernelLogText(LOG_DEBUG, TEXT("[InitShellContext] Enter"));

    This->Component = 0;
    This->CommandChar = 0;

    for (Index = 0; Index < NUM_BUFFERS; Index++) {
        This->Buffer[Index] = (LPSTR)HeapAlloc(BUFFER_SIZE);
    }

    //-------------------------------------
    // Find a starting volume

    if (Kernel.FileSystem->First) {
        LPFILESYSTEM FileSystem = (LPFILESYSTEM)Kernel.FileSystem->First;
        StringCopy(This->CurrentVolume, FileSystem->Name);
    } else {
        StringCopy(This->CurrentVolume, TEXT("??"));
    }

    StringCopy(This->CurrentFolder, TEXT(""));

    KernelLogText(LOG_DEBUG, TEXT("[InitShellContext] Exit"));
}

/***************************************************************************/

static void DeinitShellContext(LPSHELLCONTEXT This) {
    U32 Index;

    KernelLogText(LOG_DEBUG, TEXT("[DeinitShellContext] Enter"));

    for (Index = 0; Index < NUM_BUFFERS; Index++) {
        if (This->Buffer[Index]) HeapFree(This->Buffer[Index]);
    }

    KernelLogText(LOG_DEBUG, TEXT("[DeinitShellContext] Exit"));
}

/***************************************************************************/

/*
static void RotateBuffers(LPSHELLCONTEXT This) {
    U32 Index = 0;

    if (This->BufferBase) {
        for (Index = 1; Index < NUM_BUFFERS; Index++) {
            MemoryCopy(This->Buffer[Index - 1], This->Buffer[Index],
                       BUFFER_SIZE);
        }
        MemoryCopy(This->Buffer[NUM_BUFFERS - 1], This->CommandLine,
                   BUFFER_SIZE);
    }
}
*/

/***************************************************************************/

static BOOL ShowPrompt(LPSHELLCONTEXT Context) {
    ConsolePrint(TEXT("\n%s:/%s>"), Context->CurrentVolume, Context->CurrentFolder);
    return TRUE;
}

/***************************************************************************/

static BOOL ParseNextComponent(LPSHELLCONTEXT Context) {
    U32 Quotes = 0;
    U32 d = 0;

    Context->Command[d] = STR_NULL;

    if (Context->CommandLine[Context->CommandChar] == STR_NULL) return TRUE;

    while (Context->CommandLine[Context->CommandChar] != STR_NULL &&
           Context->CommandLine[Context->CommandChar] <= STR_SPACE) {
        Context->CommandChar++;
    }

    while (1) {
        if (Context->CommandLine[Context->CommandChar] == STR_NULL) {
            break;
        } else if (Context->CommandLine[Context->CommandChar] <= STR_SPACE) {
            if (Quotes == 0) {
                Context->CommandChar++;
                break;
            }
        } else if (Context->CommandLine[Context->CommandChar] == STR_QUOTE) {
            Context->CommandChar++;
            if (Quotes == 0)
                Quotes = 1;
            else
                break;
        }

        Context->Command[d] = Context->CommandLine[Context->CommandChar];

        Context->CommandChar++;
        d++;
    }

    Context->Component++;
    Context->Command[d] = STR_NULL;

    return TRUE;
}

/***************************************************************************/

static LPFILESYSTEM GetCurrentFileSystem(LPSHELLCONTEXT Context) {
    LPLISTNODE Node;
    LPFILESYSTEM FileSystem;

    for (Node = Kernel.FileSystem->First; Node; Node = Node->Next) {
        FileSystem = (LPFILESYSTEM)Node;

        if (StringCompareNC(FileSystem->Name, Context->CurrentVolume) == 0) {
            return FileSystem;
        }
    }

    return NULL;
}

/***************************************************************************/

BOOL QualifyFileName(LPSHELLCONTEXT Context, LPCSTR RawName, LPSTR FileName) {
    if (StringFindChar(RawName, STR_COLON)) {
        StringCopy(FileName, RawName);
    } else {
        LPFILESYSTEM FileSystem;

        FileSystem = GetCurrentFileSystem(Context);

        if (FileSystem == NULL) return FALSE;

        StringCopy(FileName, TEXT(FileSystem->Name));
        StringConcat(FileName, TEXT(":/"));

        if (StringLength(Context->CurrentFolder)) {
            StringConcat(FileName, (LPCSTR)Context->CurrentFolder);
            StringConcat(FileName, TEXT("/"));
        }

        StringConcat(FileName, (LPCSTR)RawName);
    }

    return TRUE;
}

/***************************************************************************/

static void ChangeFolder(LPSHELLCONTEXT Context) {
    LPFILESYSTEM FileSystem;
    LPFILE File;
    LPSTR Slash;
    FILEINFO Find;
    U32 GoingUp = 0;

    ParseNextComponent(Context);

    if (StringLength(Context->Command) == 0) {
        ConsolePrint(TEXT("Missing argument\n"));
        return;
    }

    FileSystem = GetCurrentFileSystem(Context);
    if (FileSystem == NULL) return;

    Find.Size = sizeof(FILEINFO);
    Find.FileSystem = FileSystem;
    Find.Attributes = FS_ATTR_FOLDER;

    if (StringCompareNC(Context->Command, TEXT("..")) == 0) {
        Slash = StringFindCharR(Context->CurrentFolder, STR_SLASH);
        if (Slash) {
            *Slash = STR_NULL;
        } else {
            StringCopy(Context->CurrentFolder, TEXT(""));
        }
        return;
    } else {
        if (StringLength(Context->CurrentFolder)) {
            StringCopy(Find.Name, Context->CurrentFolder);
            StringConcat(Find.Name, TEXT("/"));
        } else {
            StringCopy(Find.Name, TEXT(""));
        }
        StringConcat(Find.Name, Context->Command);
    }

    File = (LPFILE)FileSystem->Driver->Command(DF_FS_OPENFILE, (U32)&Find);

    if (File != NULL) {
        if (GoingUp == 0) {
            if (StringLength(Context->CurrentFolder)) {
                StringConcat(Context->CurrentFolder, TEXT("/"));
            }
            StringConcat(Context->CurrentFolder, File->Name);
        } else {
            StringCopy(Context->CurrentFolder, Find.Name);
        }

        FileSystem->Driver->Command(DF_FS_CLOSEFILE, (U32)File);
    } else {
        ConsolePrint(TEXT("Unknown folder : %s\n"), Find.Name);
    }
}

/***************************************************************************/

static void MakeFolder(LPSHELLCONTEXT Context) {
    LPFILESYSTEM FileSystem;
    FILEINFO FileInfo;
    STR FileName[MAX_PATH_NAME];
    LPSTR Colon;

    ParseNextComponent(Context);

    if (StringLength(Context->Command) == 0) {
        ConsolePrint(TEXT("Missing argument\n"));
        return;
    }

    FileSystem = GetCurrentFileSystem(Context);
    if (FileSystem == NULL) return;

    // StringCopy(FileName, Context->Command);

    if (QualifyFileName(Context, Context->Command, FileName)) {
        Colon = StringFindChar(FileName, STR_COLON);

        if (Colon == NULL) return;

        FileInfo.Size = sizeof(FILEINFO);
        FileInfo.FileSystem = FileSystem;
        FileInfo.Attributes = MAX_U32;

        StringCopy(FileInfo.Name, Colon + 2);

        FileSystem->Driver->Command(DF_FS_CREATEFOLDER, (U32)&FileInfo);
    }
}

/***************************************************************************/

static void ListFile(LPFILE File) {
    STR Name[MAX_FILE_NAME];
    U32 MaxWidth = 80;
    U32 Length;
    U32 Index;

    //-------------------------------------
    // Eliminate the . and .. files

    if (StringCompareNC(File->Name, (LPCSTR) ".") == 0) return;
    if (StringCompareNC(File->Name, (LPCSTR) "..") == 0) return;

    StringCopy(Name, File->Name);

    if (StringLength(Name) > (MaxWidth / 2)) {
        Index = (MaxWidth / 2) - 4;
        Name[Index++] = STR_DOT;
        Name[Index++] = STR_DOT;
        Name[Index++] = STR_DOT;
        Name[Index++] = STR_NULL;
    }

    Length = (MaxWidth / 2) - StringLength(Name);

    // Print name

    ConsolePrint(Name);
    for (Index = 0; Index < Length; Index++) ConsolePrint(TEXT(" "));

    // Print size

    if (File->Attributes & FS_ATTR_FOLDER) {
        ConsolePrint(TEXT("%12s"), TEXT("<Folder>"));
    } else {
        ConsolePrint(TEXT("%12d"), File->SizeLow);
    }

    ConsolePrint(
        TEXT(" %02d-%02d-%04d %02d:%02d "), File->Creation.Day, File->Creation.Month, File->Creation.Year,
        File->Creation.Hour, File->Creation.Minute);

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

    ConsolePrint(Text_NewLine);
}

/***************************************************************************/

static void CMD_commands(LPSHELLCONTEXT Context) {
    UNUSED(Context);

    U32 Index;

    for (Index = 0; COMMANDS[Index].Command != NULL; Index++) {
        ConsolePrint(TEXT("%s %s\n"), COMMANDS[Index].Name, COMMANDS[Index].Usage);
    }
}

/***************************************************************************/

static void CMD_cls(LPSHELLCONTEXT Context) {
    UNUSED(Context);

    ClearConsole();
}

/***************************************************************************/

static void CMD_dir(LPSHELLCONTEXT Context) {
    FILEINFO Find;
    LPFILESYSTEM FileSystem = NULL;
    LPFILE File = NULL;
    U32 Pause = 0;
    U32 NumListed = 0;

    while (1) {
        ParseNextComponent(Context);

        if (StringLength(Context->Command) == 0) break;

        if (Context->Command[0] == STR_SLASH || Context->Command[0] == STR_MINUS) {
            switch (Context->Command[1]) {
                case 'p':
                case 'P':
                    Pause = 1;
                    break;
            }
        }
    }

    FileSystem = GetCurrentFileSystem(Context);

    if (FileSystem == NULL || FileSystem->Driver == NULL) {
        ConsolePrint(TEXT("No file system mounted !\n"));
        return;
    }

    Find.Size = sizeof(FILEINFO);
    Find.FileSystem = FileSystem;
    Find.Attributes = MAX_U32;

    if (StringLength(Context->CurrentFolder)) {
        StringCopy(Find.Name, Context->CurrentFolder);
        StringConcat(Find.Name, TEXT("/"));
    } else {
        StringCopy(Find.Name, TEXT(""));
    }

    if (StringLength(Context->Command)) {
        // StringConcat(Find.Name, Context->Command);
        StringConcat(Find.Name, TEXT("*"));
    } else {
        StringConcat(Find.Name, TEXT("*"));
    }

    File = (LPFILE)FileSystem->Driver->Command(DF_FS_OPENFILE, (U32)&Find);

    if (File) {
        ListFile(File);
        while (FileSystem->Driver->Command(DF_FS_OPENNEXT, (U32)File) == DF_ERROR_SUCCESS) {
            ListFile(File);
            if (Pause) {
                NumListed++;
                if (NumListed >= Console.Height - 2) {
                    NumListed = 0;
                    WaitKey();
                }
            }
        }

        FileSystem->Driver->Command(DF_FS_CLOSEFILE, (U32)File);
    }
}

/***************************************************************************/

static void CMD_cd(LPSHELLCONTEXT Context) { ChangeFolder(Context); }

/***************************************************************************/

static void CMD_md(LPSHELLCONTEXT Context) { MakeFolder(Context); }

/***************************************************************************/

static void CMD_run(LPSHELLCONTEXT Context) {
    PROCESSINFO ProcessInfo;
    STR FileName[MAX_PATH_NAME];

    ParseNextComponent(Context);

    if (StringLength(Context->Command)) {
        if (QualifyFileName(Context, Context->Command, FileName)) {
            ProcessInfo.Flags = 0;
            ProcessInfo.FileName = FileName;
            ProcessInfo.CommandLine = NULL;
            ProcessInfo.StdOut = NULL;
            ProcessInfo.StdIn = NULL;
            ProcessInfo.StdErr = NULL;

            CreateProcess(&ProcessInfo);
        }
    }
}

/***************************************************************************/

static void CMD_exit(LPSHELLCONTEXT Context) { UNUSED(Context); }

/***************************************************************************/

static void CMD_sysinfo(LPSHELLCONTEXT Context) {
    UNUSED(Context);

    SYSTEMINFO Info;

    Info.Size = sizeof Info;
    DoSystemCall(SYSCALL_GetSystemInfo, (U32)&Info);

    ConsolePrint((LPCSTR) "Total physical memory     : %d KB\n", Info.TotalPhysicalMemory / 1024);
    ConsolePrint((LPCSTR) "Physical memory used      : %d KB\n", Info.PhysicalMemoryUsed / 1024);
    ConsolePrint((LPCSTR) "Physical memory available : %d KB\n", Info.PhysicalMemoryAvail / 1024);
    ConsolePrint((LPCSTR) "Total swap memory         : %d KB\n", Info.TotalSwapMemory / 1024);
    ConsolePrint((LPCSTR) "Swap memory used          : %d KB\n", Info.SwapMemoryUsed / 1024);
    ConsolePrint((LPCSTR) "Swap memory available     : %d KB\n", Info.SwapMemoryAvail / 1024);
    ConsolePrint((LPCSTR) "Total memory available    : %d KB\n", Info.TotalMemoryAvail / 1024);
    ConsolePrint((LPCSTR) "Processor page size       : %d Bytes\n", Info.PageSize);
    ConsolePrint((LPCSTR) "Total physical pages      : %d Pages\n", Info.TotalPhysicalPages);
    ConsolePrint((LPCSTR) "Minimum linear address    : %08X\n", Info.MinimumLinearAddress);
    ConsolePrint((LPCSTR) "Maximum linear address    : %08X\n", Info.MaximumLinearAddress);
    ConsolePrint((LPCSTR) "User name                 : %s\n", Info.UserName);
    ConsolePrint((LPCSTR) "Company name              : %s\n", Info.CompanyName);
    ConsolePrint((LPCSTR) "Number of processes       : %d\n", Info.NumProcesses);
    ConsolePrint((LPCSTR) "Number of tasks           : %d\n", Info.NumTasks);
    ConsolePrint((LPCSTR) "Stub address              : %p\n", StubAddress);
    ConsolePrint((LPCSTR) "Loader SS                 : %04X\n", KernelStartup.Loader_SS);
    ConsolePrint((LPCSTR) "Loader SP                 : %04X\n", KernelStartup.Loader_SP);
}

/***************************************************************************/

static void CMD_killtask(LPSHELLCONTEXT Context) {
    U32 TaskNum = 0;
    LPTASK Task = NULL;
    ParseNextComponent(Context);
    TaskNum = StringToU32(Context->Command);
    Task = (LPTASK)ListGetItem(Kernel.Task, TaskNum);
    if (Task) KillTask(Task);
}

/***************************************************************************/

static void CMD_showprocess(LPSHELLCONTEXT Context) {
    LPPROCESS Process;
    ParseNextComponent(Context);
    Process = ListGetItem(Kernel.Process, StringToU32(Context->Command));
    if (Process) DumpProcess(Process);
}

/***************************************************************************/

static void CMD_showtask(LPSHELLCONTEXT Context) {
    LPTASK Task;
    ParseNextComponent(Context);
    Task = ListGetItem(Kernel.Task, StringToU32(Context->Command));
    if (Task) DumpTask(Task);
}

/***************************************************************************/

static void CMD_memedit(LPSHELLCONTEXT Context) {
    ParseNextComponent(Context);
    MemEdit(StringToU32(Context->Command));
}

/***************************************************************************/

static void CMD_cat(LPSHELLCONTEXT Context) {
    FILEOPENINFO FileOpenInfo;
    FILEOPERATION FileOperation;
    STR FileName[MAX_PATH_NAME];
    HANDLE Handle;
    U32 FileSize;
    U8* Buffer;

    ParseNextComponent(Context);

    if (StringLength(Context->Command)) {
        if (QualifyFileName(Context, Context->Command, FileName)) {
            FileOpenInfo.Name = FileName;
            FileOpenInfo.Flags = FILE_OPEN_READ | FILE_OPEN_EXISTING;

            Handle = DoSystemCall(SYSCALL_OpenFile, (U32)&FileOpenInfo);

            if (Handle) {
                FileSize = DoSystemCall(SYSCALL_GetFileSize, Handle);

                if (FileSize) {
                    Buffer = (U8*)HeapAlloc(FileSize + 1);

                    if (Buffer) {
                        FileOperation.Size = sizeof(FILEOPERATION);
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
}

/***************************************************************************/

static void CMD_copy(LPSHELLCONTEXT Context) {
    U8 Buffer[1024];
    FILEOPENINFO FileOpenInfo;
    FILEOPERATION FileOperation;
    STR SrcName[MAX_PATH_NAME];
    STR DstName[MAX_PATH_NAME];
    HANDLE SrcFile;
    HANDLE DstFile;
    U32 FileSize;
    U32 BytesToRead;
    U32 Index;

    ParseNextComponent(Context);
    if (QualifyFileName(Context, Context->Command, SrcName) == 0) return;

    ParseNextComponent(Context);
    if (QualifyFileName(Context, Context->Command, DstName) == 0) return;

    ConsolePrint(TEXT("%s %s\n"), SrcName, DstName);

    FileOpenInfo.Size = sizeof(FILEOPENINFO);
    FileOpenInfo.Name = SrcName;
    FileOpenInfo.Flags = FILE_OPEN_READ | FILE_OPEN_EXISTING;
    SrcFile = DoSystemCall(SYSCALL_OpenFile, (U32)&FileOpenInfo);
    if (SrcFile == NULL) return;

    FileOpenInfo.Size = sizeof(FILEOPENINFO);
    FileOpenInfo.Name = DstName;
    FileOpenInfo.Flags = FILE_OPEN_WRITE;
    DstFile = DoSystemCall(SYSCALL_OpenFile, (U32)&FileOpenInfo);
    if (DstFile == NULL) {
        DoSystemCall(SYSCALL_DeleteObject, SrcFile);
        return;
    }

    FileSize = DoSystemCall(SYSCALL_GetFileSize, SrcFile);

    if (FileSize != 0) {
        for (Index = 0; Index < FileSize; Index += 1024) {
            BytesToRead = 1024;
            if (Index + 1024 > FileSize) BytesToRead = FileSize - Index;

            FileOperation.Size = sizeof(FILEOPERATION);
            FileOperation.File = SrcFile;
            FileOperation.NumBytes = BytesToRead;
            FileOperation.Buffer = Buffer;

            if (ReadFile(&FileOperation) != BytesToRead) break;

            FileOperation.Size = sizeof(FILEOPERATION);
            FileOperation.File = DstFile;
            FileOperation.NumBytes = BytesToRead;
            FileOperation.Buffer = Buffer;

            if (WriteFile(&FileOperation) != BytesToRead) break;
        }
    }

    DoSystemCall(SYSCALL_DeleteObject, SrcFile);
    DoSystemCall(SYSCALL_DeleteObject, DstFile);

    return;
}

/***************************************************************************/

static void CMD_edit(LPSHELLCONTEXT Context) {
    LPSTR Arguments[2];
    STR FileName[MAX_PATH_NAME];

    ParseNextComponent(Context);

    if (StringLength(Context->Command)) {
        if (QualifyFileName(Context, Context->Command, FileName)) {
            Arguments[0] = FileName;
            Edit(1, (LPCSTR*)Arguments);
        }
    } else {
        Edit(0, NULL);
    }
}

/***************************************************************************/

static void CMD_hd(LPSHELLCONTEXT Context) {
    UNUSED(Context);

    LPLISTNODE Node;
    LPPHYSICALDISK Disk;
    DISKINFO DiskInfo;

    for (Node = Kernel.Disk->First; Node; Node = Node->Next) {
        Disk = (LPPHYSICALDISK)Node;

        DiskInfo.Disk = Disk;
        Disk->Driver->Command(DF_DISK_GETINFO, (U32)&DiskInfo);

        ConsolePrint(TEXT("Designer     : %s\n"), Disk->Driver->Designer);
        ConsolePrint(TEXT("Manufacturer : %s\n"), Disk->Driver->Manufacturer);
        ConsolePrint(TEXT("Product      : %s\n"), Disk->Driver->Product);
        ConsolePrint(TEXT("Sectors      : %d\n"), DiskInfo.NumSectors);
        ConsolePrint(TEXT("\n"));
    }
}

/***************************************************************************/

static void CMD_filesystem(LPSHELLCONTEXT Context) {
    UNUSED(Context);

    LPLISTNODE Node;
    LPFILESYSTEM FileSystem;

    for (Node = Kernel.FileSystem->First; Node; Node = Node->Next) {
        FileSystem = (LPFILESYSTEM)Node;

        ConsolePrint(TEXT("Name         : %s\n"), FileSystem->Name);
        ConsolePrint(TEXT("Designer     : %s\n"), FileSystem->Driver->Designer);
        ConsolePrint(TEXT("Manufacturer : %s\n"), FileSystem->Driver->Manufacturer);
        ConsolePrint(TEXT("Product      : %s\n"), FileSystem->Driver->Product);
        ConsolePrint(TEXT("\n"));
    }
}

/***************************************************************************/

static void CMD_irq(LPSHELLCONTEXT Context) {
    UNUSED(Context);

    ConsolePrint(TEXT("8259-1 RM mask : %08b\n"), IRQMask_21_RM);
    ConsolePrint(TEXT("8259-2 RM mask : %08b\n"), IRQMask_A1_RM);
    ConsolePrint(TEXT("8259-1 PM mask : %08b\n"), IRQMask_21);
    ConsolePrint(TEXT("8259-2 PM mask : %08b\n"), IRQMask_A1);
}

/***************************************************************************/

static void CMD_outp(LPSHELLCONTEXT Context) {
    U32 Port, Data;
    ParseNextComponent(Context);
    Port = StringToU32(Context->Command);
    ParseNextComponent(Context);
    Data = StringToU32(Context->Command);
    OutPortByte(Port, Data);
}

/***************************************************************************/

static void CMD_inp(LPSHELLCONTEXT Context) {
    U32 Port, Data;
    ParseNextComponent(Context);
    Port = StringToU32(Context->Command);
    Data = InPortByte(Port);
    ConsolePrint(TEXT("Port %04X = %02X\n"), Port, Data);
}

/***************************************************************************/

static void CMD_reboot(LPSHELLCONTEXT Context) {
    UNUSED(Context);

    Reboot();
}

/***************************************************************************/

static void CMD_dos(LPSHELLCONTEXT Context) {
    UNUSED(Context);

    ClearConsole();
    Exit_EXOS(KernelStartup.Loader_SS, KernelStartup.Loader_SP);
}

/***************************************************************************/

static BOOL ParseCommand(LPSHELLCONTEXT Context) {
    U32 Length;
    U32 Index;

    KernelLogText(LOG_DEBUG, TEXT("[ParseCommand] Enter"));

    ShowPrompt(Context);

    Context->Component = 0;
    Context->CommandChar = 0;

    MemorySet(Context->CommandLine, 0, sizeof Context->CommandLine);

    KernelLogText(LOG_DEBUG, TEXT("[ParseCommand] Context->CommandLine cleared"));

    ConsoleGetString(Context->CommandLine, sizeof Context->CommandLine);

    KernelLogText(LOG_DEBUG, TEXT("[ParseCommand] Got a string"));

    // RotateBuffers(Context);

    ParseNextComponent(Context);

    Length = StringLength(Context->Command);

    if (Length == 0) return TRUE;

    //-------------------------------------
    // First see if we're going on another file system

    if (Context->Command[Length - 1] == STR_COLON) {
        LPLISTNODE Node;
        LPFILESYSTEM FileSystem;

        Context->Command[Length - 1] = STR_NULL;

        for (Node = Kernel.FileSystem->First; Node; Node = Node->Next) {
            FileSystem = (LPFILESYSTEM)Node;

            if (StringCompareNC(FileSystem->Name, Context->Command) == 0) {
                StringCopy(Context->CurrentVolume, FileSystem->Name);
                StringCopy(Context->CurrentFolder, TEXT(""));
            }
        }

        return TRUE;
    }

    //-------------------------------------

    for (Index = 0; COMMANDS[Index].Command != NULL; Index++) {
        if (StringEmpty(Context->Command) == FALSE) {
            if (StringCompareNC(Context->Command, COMMANDS[Index].Name) == 0 ||
                StringCompareNC(Context->Command, COMMANDS[Index].AltName) == 0) {
                COMMANDS[Index].Command(Context);
            }
        }
    }

    KernelLogText(LOG_DEBUG, TEXT("[ParseCommand] Exit"));

    return TRUE;
}

/***************************************************************************/

U32 Shell(LPVOID Param) {
    UNUSED(Param);
    SHELLCONTEXT Context;

    KernelLogText(LOG_DEBUG, TEXT("[Shell] Enter"));

    InitShellContext(&Context);

    ConsolePrint(Text_NewLine);

    while (ParseCommand(&Context)) {
    }

    ConsolePrint(TEXT("Exiting shell...\n"));

    DeinitShellContext(&Context);

    KernelLogText(LOG_DEBUG, TEXT("[Shell] Exit"));

    return 1;
}

/***************************************************************************/
