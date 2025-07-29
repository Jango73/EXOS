
// Shell.c

/***************************************************************************\

  EXOS Kernel
  Copyright (c) 1999 Exelsius
  All rights reserved

\***************************************************************************/

#include "Base.h"
#include "List.h"
#include "String.h"
#include "Heap.h"
#include "Console.h"
#include "FileSys.h"
#include "HD.h"
#include "Kernel.h"
#include "GFX.h"
#include "User.h"

/***************************************************************************/

#define NUM_BUFFERS 8
#define BUFFER_SIZE 1024

/***************************************************************************/

typedef struct tag_SHELLCONTEXT
{
  U32    Component;
  U32    CommandChar;
  STR    CommandLine [BUFFER_SIZE];
  STR    Command [256];
  STR    CurrentVolume [MAX_FS_LOGICAL_NAME];
  STR    CurrentFolder [MAX_PATH_NAME];
  LPVOID BufferBase;
  U32    BufferSize;
  LPSTR  Buffer [NUM_BUFFERS];
} SHELLCONTEXT, *LPSHELLCONTEXT;

/***************************************************************************/

typedef void (*SHELLCOMMAND) (LPSHELLCONTEXT);

static void CMD_commands (LPSHELLCONTEXT);
static void CMD_cls      (LPSHELLCONTEXT);
static void CMD_dir      (LPSHELLCONTEXT);
static void CMD_cd       (LPSHELLCONTEXT);
static void CMD_md       (LPSHELLCONTEXT);
static void CMD_run      (LPSHELLCONTEXT);
static void CMD_exit     (LPSHELLCONTEXT);

static struct
{
  STR          Name [16];
  STR          Usage [32];
  SHELLCOMMAND Command;
} COMMANDS [] =
{
  { "commands", "",            CMD_commands },
  { "cls",      "",            CMD_cls },
  { "dir",      "[Name] [/P]", CMD_dir },
  { "cd",       "Name",        CMD_cd },
  { "md",       "Name",        CMD_md },
  { "run",      "",            CMD_run },
  { "exit",     "",            CMD_exit },
  { "",         "",            NULL },
};

/***************************************************************************/

static void InitShellContext (LPSHELLCONTEXT This)
{
  U32 Index;

  This->Component   = 0;
  This->CommandChar = 0;

  for (Index = 0; Index < NUM_BUFFERS; Index++)
  {
    This->Buffer[Index] = (LPSTR) HeapAlloc(BUFFER_SIZE);
  }

  //-------------------------------------
  // Find a starting volume

  if (Kernel.FileSystem->First)
  {
    LPFILESYSTEM FileSystem = (LPFILESYSTEM) Kernel.FileSystem->First;
    StringCopy(This->CurrentVolume, FileSystem->Name);
  }
  else
  {
    StringCopy(This->CurrentVolume, "??");
  }

  StringCopy(This->CurrentFolder, "");
}

/***************************************************************************/

static void DeinitShellContext (LPSHELLCONTEXT This)
{
  U32 Index;

  for (Index = 0; Index < NUM_BUFFERS; Index++)
  {
    if (This->Buffer[Index]) HeapFree(This->Buffer[Index]);
  }
}

/***************************************************************************/

static void RotateBuffers (LPSHELLCONTEXT This)
{
  U32 Index = 0;

  if (This->BufferBase)
  {
    for (Index = 1; Index < NUM_BUFFERS; Index++)
    {
      MemoryCopy(This->Buffer[Index - 1], This->Buffer[Index], BUFFER_SIZE);
    }
    MemoryCopy(This->Buffer[NUM_BUFFERS - 1], This->CommandLine, BUFFER_SIZE);
  }
}

/***************************************************************************/

static BOOL ShowPrompt (LPSHELLCONTEXT Context)
{
  KernelPrint
  (
    "\n%s:/%s>",
    Context->CurrentVolume,
    Context->CurrentFolder
  );

  return TRUE;
}

/***************************************************************************/

static BOOL ParseNextComponent (LPSHELLCONTEXT Context)
{
  U32 Quotes = 0;
  U32 d = 0;
  U32 Index = 0;

  Context->Command[d] = STR_NULL;

  if (Context->CommandLine[Context->CommandChar] == STR_NULL) return TRUE;

  while
  (
    Context->CommandLine[Context->CommandChar] != STR_NULL &&
    Context->CommandLine[Context->CommandChar] <= STR_SPACE
  )
  {
    Context->CommandChar++;
  }

  while (1)
  {
    if (Context->CommandLine[Context->CommandChar] == STR_NULL)
    {
      break;
    }
    else
    if (Context->CommandLine[Context->CommandChar] <= STR_SPACE)
    {
      if (Quotes == 0)
      {
        Context->CommandChar++;
        break;
      }
    }
    else
    if (Context->CommandLine[Context->CommandChar] == STR_QUOTE)
    {
      Context->CommandChar++;
      if (Quotes == 0) Quotes = 1; else break;
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

static LPFILESYSTEM GetCurrentFileSystem (LPSHELLCONTEXT Context)
{
  LPLISTNODE Node;
  LPFILESYSTEM FileSystem;

  for (Node = Kernel.FileSystem->First; Node; Node = Node->Next)
  {
    FileSystem = (LPFILESYSTEM) Node;

    if (StringCompareNC(FileSystem->Name, Context->CurrentVolume) == 0)
    {
      return FileSystem;
    }
  }

  return NULL;
}

/***************************************************************************/

BOOL QualifyFileName (LPSHELLCONTEXT Context, LPCSTR RawName, LPSTR FileName)
{
  if (StringFindChar(RawName, STR_COLON))
  {
    StringCopy(FileName, RawName);
  }
  else
  {
    LPFILESYSTEM FileSystem;

    FileSystem = GetCurrentFileSystem(Context);

    if (FileSystem == NULL) return FALSE;

    StringCopy(FileName, FileSystem->Name);
    StringConcat(FileName, ":/");

    if (StringLength(Context->CurrentFolder))
    {
      StringConcat(FileName, Context->CurrentFolder);
      StringConcat(FileName, "/");
    }

    StringConcat(FileName, RawName);
  }

  return TRUE;
}

/***************************************************************************/

static void ChangeFolder (LPSHELLCONTEXT Context)
{
  LPLISTNODE   Node;
  LPFILESYSTEM FileSystem;
  LPFILE       File;
  LPSTR        Slash;
  FILEINFO     Find;
  U32          GoingUp = 0;

  ParseNextComponent(Context);

  if (StringLength(Context->Command) == 0)
  {
    KernelPrint("Missing argument\n");
    return;
  }

  FileSystem = GetCurrentFileSystem(Context);
  if (FileSystem == NULL) return;

  Find.Size       = sizeof(FILEINFO);
  Find.FileSystem = FileSystem;
  Find.Attributes = FS_ATTR_FOLDER;

  if (StringCompareNC(Context->Command, "..") == 0)
  {
    Slash = StringFindCharR(Context->CurrentFolder, STR_SLASH);
    if (Slash)
    {
      *Slash = STR_NULL;
    }
    else
    {
      StringCopy(Context->CurrentFolder, "");
    }
    return;
  }
  else
  {
    if (StringLength(Context->CurrentFolder))
    {
      StringCopy(Find.Name, Context->CurrentFolder);
      StringConcat(Find.Name, "/");
    }
    else
    {
      StringCopy(Find.Name, "");
    }
    StringConcat(Find.Name, Context->Command);
  }

  File = (LPFILE) FileSystem->Driver->Command(DF_FS_OPENFILE, (U32) &Find);

  if (File != NULL)
  {
    if (GoingUp == 0)
    {
      if (StringLength(Context->CurrentFolder))
      {
        StringConcat(Context->CurrentFolder, "/");
      }
      StringConcat(Context->CurrentFolder, File->Name);
    }
    else
    {
      StringCopy(Context->CurrentFolder, Find.Name);
    }

    FileSystem->Driver->Command(DF_FS_CLOSEFILE, (U32) File);
  }
  else
  {
    KernelPrint("Unknown folder : %s\n", Find.Name);
  }
}

/***************************************************************************/

static void MakeFolder (LPSHELLCONTEXT Context)
{
  LPFILESYSTEM FileSystem;
  FILEINFO     FileInfo;
  STR          FileName [MAX_PATH_NAME];
  LPSTR        Colon;

  ParseNextComponent(Context);

  if (StringLength(Context->Command) == 0)
  {
    KernelPrint("Missing argument\n");
    return;
  }

  FileSystem = GetCurrentFileSystem(Context);
  if (FileSystem == NULL) return;

  // StringCopy(FileName, Context->Command);

  if (QualifyFileName(Context, Context->Command, FileName))
  {
    Colon = StringFindChar(FileName, STR_COLON);

    if (Colon == NULL) return;

    FileInfo.Size       = sizeof(FILEINFO);
    FileInfo.FileSystem = FileSystem;
    FileInfo.Attributes = MAX_U32;

    StringCopy(FileInfo.Name, Colon + 2);

    FileSystem->Driver->Command(DF_FS_CREATEFOLDER, (U32) &FileInfo);
  }
}

/***************************************************************************/

static void ListFile (LPFILE File)
{
  STR Name [MAX_FILE_NAME];
  U32 MaxWidth = 80;
  U32 Length;
  U32 Index;

  //-------------------------------------
  // Eliminate the . and .. files

  if (StringCompareNC(File->Name, ".") == 0) return;
  if (StringCompareNC(File->Name, "..") == 0) return;

  StringCopy(Name, File->Name);

  if (StringLength(Name) > (MaxWidth / 2))
  {
    Index = (MaxWidth / 2) - 4;
    Name[Index++] = STR_DOT;
    Name[Index++] = STR_DOT;
    Name[Index++] = STR_DOT;
    Name[Index++] = STR_NULL;
  }

  Length = (MaxWidth / 2) - StringLength(Name);

  // Print name

  KernelPrint(Name);
  for (Index = 0; Index < Length; Index++) KernelPrint(" ");

  // Print size

  if (File->Attributes & FS_ATTR_FOLDER)
  {
    KernelPrint("%12s" , "<Folder>");
  }
  else
  {
    KernelPrint("%12d", File->SizeLow);
  }

  KernelPrint
  (
    " %02d-%02d-%04d %02d:%02d ",
    File->Creation.Day,
    File->Creation.Month,
    File->Creation.Year,
    File->Creation.Hour,
    File->Creation.Minute
  );

  // Print attributes

  if (File->Attributes & FS_ATTR_READONLY) KernelPrint("R"); else KernelPrint("-");
  if (File->Attributes & FS_ATTR_HIDDEN)   KernelPrint("H"); else KernelPrint("-");
  if (File->Attributes & FS_ATTR_SYSTEM)   KernelPrint("S"); else KernelPrint("-");

  KernelPrint(Text_NewLine);
}

/***************************************************************************/

static void CMD_commands (LPSHELLCONTEXT Context)
{
  U32 Index;

  for (Index = 0; COMMANDS[Index].Command != NULL; Index++)
  {
    KernelPrint("%s %s\n", COMMANDS[Index].Name, COMMANDS[Index].Usage);
  }
}

/***************************************************************************/

static void CMD_cls (LPSHELLCONTEXT Context)
{
  ClearConsole();
}

/***************************************************************************/

static void CMD_dir (LPSHELLCONTEXT Context)
{
  FILEINFO     Find;
  LPFILESYSTEM FileSystem = NULL;
  LPFILE       File       = NULL;
  U32          Pause      = 0;
  U32          NumListed  = 0;

  while (1)
  {
    ParseNextComponent(Context);

    if (StringLength(Context->Command) == 0) break;

    if (Context->Command[0] == STR_SLASH || Context->Command[0] == STR_MINUS)
    {
      switch (Context->Command[1])
      {
        case 'p' :
        case 'P' : Pause = 1; break;
      }
    }
  }

  FileSystem = GetCurrentFileSystem(Context);

  if (FileSystem == NULL)
  {
    KernelPrint("No file system mounted !\n");
    return;
  }

  Find.Size       = sizeof(FILEINFO);
  Find.FileSystem = FileSystem;
  Find.Attributes = MAX_U32;

  if (StringLength(Context->CurrentFolder))
  {
    StringCopy(Find.Name, Context->CurrentFolder);
    StringConcat(Find.Name, "/");
  }
  else
  {
    StringCopy(Find.Name, "");
  }

  if (StringLength(Context->Command))
  {
    // StringConcat(Find.Name, Context->Command);
    StringConcat(Find.Name, "*");
  }
  else
  {
    StringConcat(Find.Name, "*");
  }

  File = (LPFILE) FileSystem->Driver->Command(DF_FS_OPENFILE, (U32) &Find);

  if (File)
  {
    ListFile(File);
    while (FileSystem->Driver->Command(DF_FS_OPENNEXT, (U32) File) == DF_ERROR_SUCCESS)
    {
      ListFile(File);
      if (Pause)
      {
        NumListed++;
        if (NumListed >= Console.Height - 2)
        {
          NumListed = 0;
          WaitKey();
        }
      }
    }

    FileSystem->Driver->Command(DF_FS_CLOSEFILE, (U32) File);
  }
}

/***************************************************************************/

static void CMD_cd (LPSHELLCONTEXT Context)
{
  ChangeFolder(Context);
}

/***************************************************************************/

static void CMD_md (LPSHELLCONTEXT Context)
{
  MakeFolder(Context);
}

/***************************************************************************/

static void CMD_run (LPSHELLCONTEXT Context)
{
  PROCESSINFO ProcessInfo;
  STR FileName [MAX_PATH_NAME];

  ParseNextComponent(Context);

  if (StringLength(Context->Command))
  {
    if (QualifyFileName(Context, Context->Command, FileName))
    {
      ProcessInfo.Flags       = 0;
      ProcessInfo.FileName    = FileName;
      ProcessInfo.CommandLine = NULL;
      ProcessInfo.StdOut      = NULL;
      ProcessInfo.StdIn       = NULL;
      ProcessInfo.StdErr      = NULL;

      CreateProcess(&ProcessInfo);
    }
  }
}

/***************************************************************************/

static void CMD_exit (LPSHELLCONTEXT Context)
{
}

/***************************************************************************/

static BOOL ParseCommand (LPSHELLCONTEXT Context)
{
  U32 Length;
  U32 Index;

  ShowPrompt(Context);

  Context->Component   = 0;
  Context->CommandChar = 0;

  MemorySet(Context->CommandLine, 0, sizeof Context->CommandLine);

  ConsoleGetString(Context->CommandLine, sizeof Context->CommandLine);

  // RotateBuffers(Context);

  ParseNextComponent(Context);

  Length = StringLength(Context->Command);

  if (Length == 0) return TRUE;

  //-------------------------------------
  // First see if we're going on another file system

  if (Context->Command[Length - 1] == STR_COLON)
  {
    LPLISTNODE Node;
    LPFILESYSTEM FileSystem;

    Context->Command[Length - 1] = STR_NULL;

    for (Node = Kernel.FileSystem->First; Node; Node = Node->Next)
    {
      FileSystem = (LPFILESYSTEM) Node;

      if (StringCompareNC(FileSystem->Name, Context->Command) == 0)
      {
        StringCopy(Context->CurrentVolume, FileSystem->Name);
        StringCopy(Context->CurrentFolder, "");
      }
    }

    return TRUE;
  }

  //-------------------------------------

  for (Index = 0; COMMANDS[Index].Command != NULL; Index++)
  {
    if (StringCompareNC(Context->Command, COMMANDS[Index].Name) == 0)
    {
      COMMANDS[Index].Command(Context);
    }
  }

  return TRUE;

/*
  if (StringCompareNC(Context->Command, "cls") == 0)
  {
    ClearConsole();
  }
  else
  if (StringCompareNC(Context->Command, "cd") == 0)
  {
    ChangeFolder(Context);
  }
  else
  if (StringCompareNC(Context->Command, "md") == 0)
  {
    MakeFolder(Context);
  }
  else
  if (StringCompareNC(Context->Command, "exit") == 0)
  {
    return FALSE;
  }
  else
  if (StringCompareNC(Context->Command, "killtask") == 0)
  {
    U32    TaskNum = 0;
    LPTASK Task    = NULL;
    ParseNextComponent(Context);
    TaskNum = StringToU32(Context->Command);
    Task = (LPTASK) ListGetItem(Kernel.Task, TaskNum);
    if (Task) KillTask(Task);
  }
  else
  if (StringCompareNC(Context->Command, "showprocess") == 0)
  {
    LPPROCESS Process;
    ParseNextComponent(Context);
    Process = ListGetItem(Kernel.Process, StringToU32(Context->Command));
    if (Process) DumpProcess(Process);
  }
  else
  if (StringCompareNC(Context->Command, "showtask") == 0)
  {
    LPTASK Task;
    ParseNextComponent(Context);
    Task = ListGetItem(Kernel.Task, StringToU32(Context->Command));
    if (Task) DumpTask(Task);
  }
  else
  if (StringCompareNC(Context->Command, "memedit") == 0)
  {
    ParseNextComponent(Context);
    MemEdit(StringToU32(Context->Command));
  }
  else
  if (StringCompareNC(Context->Command, "sysinfo") == 0)
  {
    SYSTEMINFO Info;

    Info.Size = sizeof Info;
    DoSystemCall(SYSCALL_GetSystemInfo, (U32) &Info);

    KernelPrint("Total physical memory     : %d KB\n",    Info.TotalPhysicalMemory / 1024);
    KernelPrint("Physical memory used      : %d KB\n",    Info.PhysicalMemoryUsed / 1024);
    KernelPrint("Physical memory available : %d KB\n",    Info.PhysicalMemoryAvail / 1024);
    KernelPrint("Total swap memory         : %d KB\n",    Info.TotalSwapMemory / 1024);
    KernelPrint("Swap memory used          : %d KB\n",    Info.SwapMemoryUsed / 1024);
    KernelPrint("Swap memory available     : %d KB\n",    Info.SwapMemoryAvail / 1024);
    KernelPrint("Total memory available    : %d KB\n",    Info.TotalMemoryAvail / 1024);
    KernelPrint("Processor page size       : %d Bytes\n", Info.PageSize);
    KernelPrint("Total physical pages      : %d Pages\n", Info.TotalPhysicalPages);
    KernelPrint("Minimum linear address    : %08X\n",     Info.MinimumLinearAddress);
    KernelPrint("Maximum linear address    : %08X\n",     Info.MaximumLinearAddress);
    KernelPrint("User name                 : %s\n",       Info.UserName);
    KernelPrint("Company name              : %s\n",       Info.CompanyName);
    KernelPrint("Number of processes       : %d\n",       Info.NumProcesses);
    KernelPrint("Number of tasks           : %d\n",       Info.NumTasks);
    KernelPrint("Stub address              : %p\n",       StubAddress);
    KernelPrint("Loader SS                 : %04X\n",     KernelStartup.Loader_SS);
    KernelPrint("Loader SP                 : %04X\n",     KernelStartup.Loader_SP);
  }
  else
  if (StringCompareNC(Context->Command, "type") == 0)
  {
    LPFILESYSTEM  FileSystem;
    FILEOPENINFO  FileOpenInfo;
    FILEOPERATION FileOperation;
    STR           FileName [MAX_PATH_NAME];
    HANDLE        Handle;
    U32           FileSize;
    U8*           Buffer;

    ParseNextComponent(Context);

    if (StringLength(Context->Command))
    {
      if (QualifyFileName(Context, Context->Command, FileName))
      {
        FileOpenInfo.Name  = FileName;
        FileOpenInfo.Flags = FILE_OPEN_READ | FILE_OPEN_EXISTING;

        Handle = DoSystemCall(SYSCALL_OpenFile, (U32) &FileOpenInfo);
        if (Handle)
        {
          FileSize = DoSystemCall(SYSCALL_GetFileSize, Handle);
          if (FileSize)
          {
            Buffer = (U8*) HeapAlloc(FileSize + 1);
            if (Buffer)
            {
              FileOperation.Size     = sizeof(FILEOPERATION);
              FileOperation.File     = Handle;
              FileOperation.NumBytes = FileSize;
              FileOperation.Buffer   = Buffer;
              if (DoSystemCall(SYSCALL_ReadFile, (U32) &FileOperation))
              {
                Buffer[FileSize] = STR_NULL;
                KernelPrint((LPSTR) Buffer);
              }
              HeapFree(Buffer);
            }
          }
          DoSystemCall(SYSCALL_DeleteObject, Handle);
        }
      }
    }
  }
  else
  if (StringCompareNC(Context->Command, "dir") == 0)
  {
    FILEINFO     Find;
    LPFILESYSTEM FileSystem = NULL;
    LPFILE       File       = NULL;
    U32          Pause      = 0;
    U32          NumListed  = 0;

    while (1)
    {
      ParseNextComponent(Context);

      if (StringLength(Context->Command) == 0) break;

      if (Context->Command[0] == STR_SLASH || Context->Command[0] == STR_MINUS)
      {
        switch (Context->Command[1])
        {
          case 'p' :
          case 'P' : Pause = 1; break;
        }
      }
    }

    FileSystem = GetCurrentFileSystem(Context);

    if (FileSystem == NULL)
    {
      KernelPrint("No file system mounted !\n");
      return FALSE;
    }

    Find.Size       = sizeof(FILEINFO);
    Find.FileSystem = FileSystem;
    Find.Attributes = MAX_U32;

    if (StringLength(Context->CurrentFolder))
    {
      StringCopy(Find.Name, Context->CurrentFolder);
      StringConcat(Find.Name, "/");
    }
    else
    {
      StringCopy(Find.Name, "");
    }

    if (StringLength(Context->Command))
    {
      // StringConcat(Find.Name, Context->Command);
      StringConcat(Find.Name, "*");
    }
    else
    {
      StringConcat(Find.Name, "*");
    }

    File = (LPFILE) FileSystem->Driver->Command(DF_FS_OPENFILE, (U32) &Find);

    if (File)
    {
      ListFile(File);
      while (FileSystem->Driver->Command(DF_FS_OPENNEXT, (U32) File) == DF_ERROR_SUCCESS)
      {
        ListFile(File);
        if (Pause)
        {
          NumListed++;
          if (NumListed >= Console.Height - 2)
          {
            NumListed = 0;
            WaitKey();
          }
        }
      }

      FileSystem->Driver->Command(DF_FS_CLOSEFILE, (U32) File);
    }
  }
  else
  if (StringCompareNC(Context->Command, "copy") == 0)
  {
    U8            Buffer [1024];
    FILEOPENINFO  FileOpenInfo;
    FILEOPERATION FileOperation;
    STR           SrcName [MAX_PATH_NAME];
    STR           DstName [MAX_PATH_NAME];
    HANDLE        SrcFile;
    HANDLE        DstFile;
    U32           FileSize;
    U32           BytesToRead;
    U32           Index;

    ParseNextComponent(Context);
    if (QualifyFileName(Context, Context->Command, SrcName) == 0) return FALSE;

    ParseNextComponent(Context);
    if (QualifyFileName(Context, Context->Command, DstName) == 0) return FALSE;

    KernelPrint("%s %s\n", SrcName, DstName);

    FileOpenInfo.Size       = sizeof(FILEOPENINFO);
    FileOpenInfo.Name       = SrcName;
    FileOpenInfo.Flags      = FILE_OPEN_READ | FILE_OPEN_EXISTING;
    SrcFile = DoSystemCall(SYSCALL_OpenFile, (U32) &FileOpenInfo);
    if (SrcFile == NULL) return FALSE;

    FileOpenInfo.Size       = sizeof(FILEOPENINFO);
    FileOpenInfo.Name       = DstName;
    FileOpenInfo.Flags      = FILE_OPEN_WRITE;
    DstFile = DoSystemCall(SYSCALL_OpenFile, (U32) &FileOpenInfo);
    if (DstFile == NULL)
    {
      DoSystemCall(SYSCALL_DeleteObject, SrcFile);
      return FALSE;
    }

    FileSize = DoSystemCall(SYSCALL_GetFileSize, SrcFile);

    if (FileSize != 0)
    {
      for (Index = 0; Index < FileSize; Index += 1024)
      {
        BytesToRead = 1024;
        if (Index + 1024 > FileSize) BytesToRead = FileSize - Index;

        FileOperation.Size     = sizeof(FILEOPERATION);
        FileOperation.File     = SrcFile;
        FileOperation.NumBytes = BytesToRead;
        FileOperation.Buffer   = Buffer;

        if (ReadFile(&FileOperation) != BytesToRead) break;

        FileOperation.Size     = sizeof(FILEOPERATION);
        FileOperation.File     = DstFile;
        FileOperation.NumBytes = BytesToRead;
        FileOperation.Buffer   = Buffer;

        if (WriteFile(&FileOperation) != BytesToRead) break;
      }
    }

    DoSystemCall(SYSCALL_DeleteObject, SrcFile);
    DoSystemCall(SYSCALL_DeleteObject, DstFile);

    return TRUE;
  }
  else
  if (StringCompareNC(Context->Command, "edit") == 0)
  {
    LPFILESYSTEM FileSystem;
    LPSTR Arguments [2];
    STR FileName [MAX_PATH_NAME];

    ParseNextComponent(Context);

    if (StringLength(Context->Command))
    {
      if (QualifyFileName(Context, Context->Command, FileName))
      {
        Arguments[0] = FileName;
        Edit(1, (LPCSTR*) Arguments);
      }
    }
    else
    {
      Edit(0, NULL);
    }
  }
  else
  if (StringCompareNC(Context->Command, "hd") == 0)
  {
    LPLISTNODE Node;
    LPPHYSICALDISK Disk;
    DISKINFO DiskInfo;

    for (Node = Kernel.Disk->First; Node; Node = Node->Next)
    {
      Disk = (LPPHYSICALDISK) Node;

      DiskInfo.Disk = Disk;
      Disk->Driver->Command(DF_DISK_GETINFO, (U32) &DiskInfo);

      KernelPrint("Designer     : %s\n", Disk->Driver->Designer);
      KernelPrint("Manufacturer : %s\n", Disk->Driver->Manufacturer);
      KernelPrint("Product      : %s\n", Disk->Driver->Product);
      KernelPrint("Sectors      : %d\n", DiskInfo.NumSectors);
      KernelPrint("\n");
    }
  }
  else
  if (StringCompareNC(Context->Command, "filesystem") == 0)
  {
    LPLISTNODE Node;
    LPFILESYSTEM FileSystem;

    for (Node = Kernel.FileSystem->First; Node; Node = Node->Next)
    {
      FileSystem = (LPFILESYSTEM) Node;

      KernelPrint("Name         : %s\n", FileSystem->Name);
      KernelPrint("Designer     : %s\n", FileSystem->Driver->Designer);
      KernelPrint("Manufacturer : %s\n", FileSystem->Driver->Manufacturer);
      KernelPrint("Product      : %s\n", FileSystem->Driver->Product);
      KernelPrint("\n");
    }
  }
  else
  if (StringCompareNC(Context->Command, "run") == 0)
  {
    PROCESSINFO ProcessInfo;
    STR FileName [MAX_PATH_NAME];

    ParseNextComponent(Context);

    if (StringLength(Context->Command))
    {
      if (QualifyFileName(Context, Context->Command, FileName))
      {
        ProcessInfo.Flags       = 0;
        ProcessInfo.FileName    = FileName;
        ProcessInfo.CommandLine = NULL;
        ProcessInfo.StdOut      = NULL;
        ProcessInfo.StdIn       = NULL;
        ProcessInfo.StdErr      = NULL;

        CreateProcess(&ProcessInfo);
      }
    }
  }
  else
  if (StringCompareNC(Context->Command, "rmc") == 0)
  {
    RealModeCallTest();
  }
  else
  if (StringCompareNC(Context->Command, "reboot") == 0)
  {
    // Reboot();
  }
  else
  if (StringCompareNC(Context->Command, "irq") == 0)
  {
    KernelPrint("8259-1 RM mask : %08b\n", IRQMask_21_RM);
    KernelPrint("8259-2 RM mask : %08b\n", IRQMask_A1_RM);
    KernelPrint("8259-1 PM mask : %08b\n", IRQMask_21);
    KernelPrint("8259-2 PM mask : %08b\n", IRQMask_A1);
  }
  else
  if (StringCompareNC(Context->Command, "outp") == 0)
  {
    U32 Port, Data;
    ParseNextComponent(Context); Port = StringToU32(Context->Command);
    ParseNextComponent(Context); Data = StringToU32(Context->Command);
    OutPortByte(Port, Data);
  }
  else
  if (StringCompareNC(Context->Command, "inp") == 0)
  {
    U32 Port, Data;
    ParseNextComponent(Context); Port = StringToU32(Context->Command);
    Data = InPortByte(Port);
    KernelPrint("Port %04X = %02X\n", Port, Data);
  }
  else
  if (StringCompareNC(Context->Command, "password") == 0)
  {
    STR Password [48];
    STR Crypted [48];
    STR Try1 [48];
    STR Try2 [48];
    U32 c, l;

    StringCopy(Password, "Prout de prout");
    StringCopy(Try1, "Zobbie la mouche");
    StringCopy(Try2, "Prout de prout");

    KernelPrint("Password : %s\n", Password);
    if (MakePassword(Password, Crypted))
    {
      KernelPrint("Crypted : ");
      l=StringLength(Crypted);
      for (c=0; c<l; c++) KernelPrint("%02x", Crypted[c]);
      KernelPrint("\n");
      KernelPrint("Test with \"%s\" = %d\n", Try1, CheckPassword(Crypted, Try1));
      KernelPrint("Test with \"%s\" = %d\n", Try2, CheckPassword(Crypted, Try2));
    }
  }
  else
  if (StringCompareNC(Context->Command, "dos") == 0)
  {
    Exit_EXOS(KernelStartup.Loader_SS, KernelStartup.Loader_SP);
  }
*/

  return TRUE;
}

/***************************************************************************/

U32 Shell (LPVOID Param)
{
  SHELLCONTEXT Context;

  InitShellContext(&Context);

  ConsolePrint(Text_NewLine);

  while (ParseCommand(&Context))
  {
  }

  ConsolePrint("Exiting shell...\n");

  DeinitShellContext(&Context);

  return 1;
}

/***************************************************************************/
