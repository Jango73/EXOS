
/***************************************************************************\

    EXOS Kernel
    Copyright (c) 1999-2025 Jango73
    All rights reserved

\***************************************************************************/

#include "../include/Process.h"

#include "../include/Address.h"
#include "../include/Console.h"
#include "../include/File.h"
#include "../include/Kernel.h"

/***************************************************************************/

PROCESS KernelProcess = {
    .ID = ID_PROCESS,
    .References = 1,
    .Next = NULL,
    .Prev = NULL,
    .Mutex = EMPTY_MUTEX,
    .HeapMutex = EMPTY_MUTEX,
    .Security = EMPTY_SECURITY,
    .Desktop = NULL,
    .Parent = NULL,
    .Privilege = PRIVILEGE_KERNEL,
    .PageDirectory = PA_PGD,
    .HeapBase = 0,
    .HeapSize = 0,
    .FileName = "EXOS",
    .CommandLine = "",
    .Objects = NULL
};

/***************************************************************************/

void InitializeKernelHeap() {
    KernelProcess.HeapSize = N_1MB;

    LINEAR HeapBase = VirtualAlloc(MAX_U32, KernelProcess.HeapSize,
                                   ALLOC_PAGES_COMMIT | ALLOC_PAGES_READWRITE);

    if (!HeapBase) {
        ClearConsole();
        SLEEPING_BEAUTY;
    }

    KernelProcess.HeapBase = (LINEAR)HeapBase;
    MemorySet((LPVOID)KernelProcess.HeapBase, 0, sizeof(HEAPCONTROLBLOCK));

    ((LPHEAPCONTROLBLOCK)KernelProcess.HeapBase)->ID = ID_HEAP;
}

/***************************************************************************/

BOOL GetExecutableInfo_EXOS(LPFILE File, LPEXECUTABLEINFO Info) {
    FILEOPERATION FileOperation;
    EXOSCHUNK Chunk;
    EXOSHEADER Header;
    EXOSCHUNK_INIT Init;
    U32 BytesRead;
    U32 Index;
    U32 Dummy;

#ifdef __DEBUG__
    KernelPrint("Entering GetExecutableInfo_EXOS\n");
#endif

    if (File == NULL) return FALSE;
    if (Info == NULL) return FALSE;

    FileOperation.Size = sizeof(FILEOPERATION);
    FileOperation.File = (HANDLE)File;

    //-------------------------------------
    // Read the header

    FileOperation.NumBytes = sizeof(EXOSHEADER);
    FileOperation.Buffer = (LPVOID)&Header;
    BytesRead = ReadFile(&FileOperation);

    if (Header.Signature != EXOS_SIGNATURE) {
#ifdef __DEBUG__
        KernelPrint("GetExecutableInfo_EXOS() : Bad signature (%08X)\n",
                    Header.Signature);
#endif

        goto Out_Error;
    }

    while (1) {
        FileOperation.NumBytes = sizeof(EXOSCHUNK);
        FileOperation.Buffer = (LPVOID)&Chunk;
        BytesRead = ReadFile(&FileOperation);

        if (BytesRead != sizeof(EXOSCHUNK)) break;

        if (Chunk.ID == EXOS_CHUNK_INIT) {
            FileOperation.NumBytes = sizeof(EXOSCHUNK_INIT);
            FileOperation.Buffer = (LPVOID)&Init;
            BytesRead = ReadFile(&FileOperation);

            if (BytesRead != sizeof(EXOSCHUNK_INIT)) goto Out_Error;

            Info->EntryPoint = Init.EntryPoint;
            Info->CodeBase = Init.CodeBase;
            Info->DataBase = Init.DataBase;
            Info->CodeSize = Init.CodeSize;
            Info->DataSize = Init.DataSize;
            Info->StackMinimum = Init.StackMinimum;
            Info->StackRequested = Init.StackRequested;
            Info->HeapMinimum = Init.HeapMinimum;
            Info->HeapRequested = Init.HeapRequested;

            goto Out_Success;
        } else {
            for (Index = 0; Index < Chunk.Size; Index++) {
                FileOperation.NumBytes = 1;
                FileOperation.Buffer = (LPVOID)&Dummy;
                BytesRead = ReadFile(&FileOperation);
            }
        }
    }

Out_Success:

#ifdef __DEBUG__
    KernelPrint("Exiting GetExecutableInfo_EXOS (Success)\n");
#endif

    return TRUE;

Out_Error:

#ifdef __DEBUG__
    KernelPrint("Exiting GetExecutableInfo_EXOS (Error)\n");
#endif

    return FALSE;
}

/***************************************************************************/

BOOL LoadExecutable_EXOS(LPFILE File, LPEXECUTABLEINFO Info, LINEAR CodeBase,
                         LINEAR DataBase) {
    FILEOPERATION FileOperation;
    EXOSCHUNK Chunk;
    EXOSHEADER Header;
    EXOSCHUNK_FIXUP Fixup;
    LINEAR ItemAddress;
    U32 BytesRead;
    U32 Index;
    U32 CodeRead;
    U32 DataRead;
    U32 CodeOffset;
    U32 DataOffset;
    U32 NumFixups;
    U32 Dummy;
    U32 c;

#ifdef __DEBUG__
    KernelPrint("Entering LoadExecutable_EXOS\n");
#endif

    if (File == NULL) return FALSE;

    FileOperation.Size = sizeof(FILEOPERATION);
    FileOperation.File = (HANDLE)File;

    CodeRead = 0;
    DataRead = 0;

    CodeOffset = CodeBase - Info->CodeBase;
    DataOffset = DataBase - Info->DataBase;

#ifdef __DEBUG__
    KernelPrint("LoadExecutable_EXOS() : CodeBase = %08X\n", CodeBase);
    KernelPrint("LoadExecutable_EXOS() : DataBase = %08X\n", DataBase);
#endif

    //-------------------------------------
    // Read the header

    FileOperation.NumBytes = sizeof(EXOSHEADER);
    FileOperation.Buffer = (LPVOID)&Header;
    BytesRead = ReadFile(&FileOperation);

    if (Header.Signature != EXOS_SIGNATURE) {
        goto Out_Error;
    }

    while (1) {
        FileOperation.NumBytes = sizeof(EXOSCHUNK);
        FileOperation.Buffer = (LPVOID)&Chunk;
        BytesRead = ReadFile(&FileOperation);

        if (BytesRead != sizeof(EXOSCHUNK)) break;

        if (Chunk.ID == EXOS_CHUNK_CODE) {
            if (CodeRead == 1) {
                //-------------------------------------
                // Only one code chunk allowed

                goto Out_Error;
            }

#ifdef __DEBUG__
            KernelPrint("LoadExecutable_EXOS() : Reading code\n");
#endif

            FileOperation.NumBytes = Chunk.Size;
            FileOperation.Buffer = (LPVOID)CodeBase;
            BytesRead = ReadFile(&FileOperation);

            if (BytesRead != Chunk.Size) goto Out_Error;

            CodeRead = 1;
        } else if (Chunk.ID == EXOS_CHUNK_DATA) {
            if (DataRead == 1) {
                //-------------------------------------
                // Only one data chunk allowed

                goto Out_Error;
            }

#ifdef __DEBUG__
            KernelPrint("LoadExecutable_EXOS() : Reading data\n");
#endif

            FileOperation.NumBytes = Chunk.Size;
            FileOperation.Buffer = (LPVOID)DataBase;
            BytesRead = ReadFile(&FileOperation);

            if (BytesRead != Chunk.Size) goto Out_Error;

            DataRead = 1;
        } else if (Chunk.ID == EXOS_CHUNK_FIXUP) {
            FileOperation.NumBytes = sizeof(U32);
            FileOperation.Buffer = (LPVOID)&NumFixups;
            BytesRead = ReadFile(&FileOperation);

            if (BytesRead != sizeof(U32)) goto Out_Error;

#ifdef __DEBUG__
            KernelPrint("LoadExecutable_EXOS() : Reading relocations\n");
#endif

            for (c = 0; c < NumFixups; c++) {
                FileOperation.NumBytes = sizeof(EXOSCHUNK_FIXUP);
                FileOperation.Buffer = (LPVOID)&Fixup;
                BytesRead = ReadFile(&FileOperation);

                if (BytesRead != sizeof(EXOSCHUNK_FIXUP)) goto Out_Error;

                if (Fixup.Section & EXOS_FIXUP_SOURCE_CODE) {
                    ItemAddress = CodeBase + (Fixup.Address - Info->CodeBase);
                } else if (Fixup.Section & EXOS_FIXUP_SOURCE_DATA) {
                    ItemAddress = DataBase + (Fixup.Address - Info->DataBase);
                } else {
                    ItemAddress = NULL;
                }

                if (ItemAddress != NULL) {
                    if (Fixup.Section & EXOS_FIXUP_DEST_CODE) {
                        *((U32*)ItemAddress) += CodeOffset;
                    } else if (Fixup.Section & EXOS_FIXUP_DEST_DATA) {
                        *((U32*)ItemAddress) += DataOffset;
                    }
                }
            }

            goto Out_Success;
        } else {
            for (Index = 0; Index < Chunk.Size; Index++) {
                FileOperation.NumBytes = 1;
                FileOperation.Buffer = (LPVOID)&Dummy;
                BytesRead = ReadFile(&FileOperation);
            }
        }
    }

    if (CodeRead == 0) goto Out_Error;

Out_Success:

#ifdef __DEBUG__
    KernelPrint("Exiting LoadExecutable_EXOS\n");
#endif

    return TRUE;

Out_Error:

#ifdef __DEBUG__
    KernelPrint("Exiting LoadExecutable_EXOS\n");
#endif

    return FALSE;
}

/***************************************************************************/

LPPROCESS NewProcess() {
    LPPROCESS This = NULL;

#ifdef __DEBUG__
    KernelPrint("Entering NewProcess\n");
#endif

    This = (LPPROCESS)KernelMemAlloc(sizeof(PROCESS));

    if (This == NULL) return NULL;

    MemorySet(This, 0, sizeof(PROCESS));

    This->ID = ID_PROCESS;
    This->References = 1;
    This->Desktop = (LPDESKTOP)Kernel.Desktop->First;
    This->Parent = GetCurrentProcess();
    // This->Privilege     = PRIVILEGE_USER;
    This->Privilege = PRIVILEGE_KERNEL;

    //-------------------------------------
    // Initialize the process' mutexs

    InitMutex(&(This->Mutex));
    InitMutex(&(This->HeapMutex));

    //-------------------------------------
    // Initialize the process' security

    InitSecurity(&(This->Security));

#ifdef __DEBUG__
    KernelPrint("Exiting NewProcess\n");
#endif

    return This;
}

/***************************************************************************/

/*
BOOL CreateProcess (LPPROCESSINFO Info)
{
  TASKINFO      TaskInfo;
  FILEOPENINFO  FileOpenInfo;
  FILEOPERATION FileOperation;
  LPPROCESS     Process       = NULL;
  LPTASK        Task          = NULL;
  LPFILE        File          = NULL;
  PHYSICAL      PageDirectory = NULL;
  U32           FileSize      = 0;
  U32           CodeSize      = 0;
  U32           DataSize      = 0;
  U32           HeapSize      = 0;
  U32           TotalSize     = 0;
  U32           BytesRead     = 0;
  BOOL          Result        = FALSE;

#ifdef __DEBUG__
  KernelPrint("Entering CreateProcess\n");
#endif

  if (Info == NULL) return FALSE;

  //-------------------------------------
  // Open the executable file

#ifdef __DEBUG__
  KernelPrint("CreateProcess() : Opening file %s\n", Info->FileName);
#endif

  FileOpenInfo.Size  = sizeof FileOpenInfo;
  FileOpenInfo.Name  = Info->FileName;
  FileOpenInfo.Flags = 0;

  File = OpenFile(&FileOpenInfo);

  if (File == NULL) return FALSE;

  //-------------------------------------
  // Read the size of the file

  FileSize = GetFileSize(File);

  if (FileSize == 0) return FALSE;

  //-------------------------------------
  // Lock access to kernel data

  LockMutex(MUTEX_KERNEL, INFINITY);

  //-------------------------------------
  // Allocate a new process structure

#ifdef __DEBUG__
  KernelPrint("CreateProcess() : Allocating process...\n");
#endif

  Process = NewProcess();
  if (Process == NULL) goto Out;

  StringCopy(Process->FileName, Info->FileName);

  CodeSize = FileSize;
  DataSize = 0;
  HeapSize = N_128KB;

  TotalSize = CodeSize + DataSize + HeapSize;

  //-------------------------------------
  // Allocate and setup the page directory

  Process->PageDirectory = AllocPageDirectory();

  //-------------------------------------

  FreezeScheduler();

  //-------------------------------------
  // We can use the new page directory from now on
  // and switch back to the previous when done

#ifdef __DEBUG__
  KernelPrint("CreateProcess() : Switching page directory...\n");
#endif

  PageDirectory = GetCurrentProcess()->PageDirectory;

  LoadPageDirectory(Process->PageDirectory);

  //-------------------------------------
  // Allocate enough memory for the code, data and heap

#ifdef __DEBUG__
  KernelPrint("CreateProcess() : VirtualAllocing process space...\n");
#endif

  VirtualAlloc(LA_USER, TotalSize, ALLOC_PAGES_COMMIT);

  //-------------------------------------
  // Load executable image
  // For tests, image must be at LA_KERNEL

#ifdef __DEBUG__
  KernelPrint("CreateProcess() : Loading executable...\n");
#endif

  FileOperation.Size     = sizeof FileOperation;
  FileOperation.File     = (HANDLE) File;
  FileOperation.NumBytes = FileSize;
  FileOperation.Buffer   = (LPVOID) LA_USER;

  BytesRead = ReadFile(&FileOperation);

  if (BytesRead != FileSize)
  {
#ifdef __DEBUG__
    KernelPrint("CreateProcess() : Load failed !\n");
#endif

    VirtualFree(LA_USER, TotalSize);
    LoadPageDirectory(PageDirectory);
    UnfreezeScheduler();
    CloseFile(File);
    goto Out;
  }

  CloseFile(File);

  //-------------------------------------
  // Initialize the heap

  Process->HeapBase = LA_USER + CodeSize + DataSize;
  Process->HeapSize = HeapSize;

  MemorySet((LPVOID) Process->HeapBase, 0, Process->HeapSize);

  *((U32*)Process->HeapBase) = ID_HEAP;

  //-------------------------------------
  // Create the initial task

#ifdef __DEBUG__
  KernelPrint("CreateProcess() : Creating initial task...\n");
#endif

  TaskInfo.Func      = (TASKFUNC) LA_USER;
  TaskInfo.Parameter = NULL;
  TaskInfo.StackSize = TASK_MINIMUM_STACK_SIZE;
  TaskInfo.Priority  = TASK_PRIORITY_MEDIUM;
  TaskInfo.Flags     = TASK_CREATE_SUSPENDED;

  Task = CreateTask(Process, &TaskInfo);

  //-------------------------------------
  // Switch back to our page directory

#ifdef __DEBUG__
  KernelPrint("CreateProcess() : Switching page directory...\n");
#endif

  LoadPageDirectory(PageDirectory);

  //-------------------------------------

  UnfreezeScheduler();

  //-------------------------------------
  // Add the new process to the kernel's process list

  ListAddItem(Kernel.Process, Process);

  //-------------------------------------
  // Add initial task to the scheduler's queue

  AddTaskToQueue(Task);

  Result = TRUE;

Out :

  Info->Process = (HANDLE) Process;
  Info->Task    = (HANDLE) Task;

  //-------------------------------------
  // Release access to kernel data

  UnlockMutex(MUTEX_KERNEL);

#ifdef __DEBUG__
  KernelPrint("Exiting CreateProcess : Result = %d\n", Result);
#endif

  return Result;
}
*/

/***************************************************************************/

BOOL CreateProcess(LPPROCESSINFO Info) {
    EXECUTABLEINFO ExecutableInfo;
    TASKINFO TaskInfo;
    FILEOPENINFO FileOpenInfo;
    LPPROCESS Process = NULL;
    LPTASK Task = NULL;
    LPFILE File = NULL;
    PHYSICAL PageDirectory = NULL;
    LINEAR CodeBase = NULL;
    LINEAR DataBase = NULL;
    LINEAR HeapBase = NULL;
    U32 FileSize = 0;
    U32 CodeSize = 0;
    U32 DataSize = 0;
    U32 HeapSize = 0;
    U32 StackSize = 0;
    U32 TotalSize = 0;
    BOOL Result = FALSE;

#ifdef __DEBUG__
    KernelPrint("Entering CreateProcess\n");
#endif

    if (Info == NULL) return FALSE;

        //-------------------------------------
        // Open the executable file

#ifdef __DEBUG__
    KernelPrint("CreateProcess() : Opening file %s\n", Info->FileName);
#endif

    FileOpenInfo.Size = sizeof(FILEOPENINFO);
    FileOpenInfo.Name = Info->FileName;
    FileOpenInfo.Flags = FILE_OPEN_READ | FILE_OPEN_EXISTING;

    File = OpenFile(&FileOpenInfo);

    if (File == NULL) return FALSE;

    //-------------------------------------
    // Read the size of the file

    FileSize = GetFileSize(File);

    if (FileSize == 0) return FALSE;

#ifdef __DEBUG__
    KernelPrint("CreateProcess() : File size %d\n", FileSize);
#endif

    //-------------------------------------
    // Get executable information

    if (GetExecutableInfo_EXOS(File, &ExecutableInfo) == FALSE) {
        return FALSE;
    }

    CloseFile(File);

    //-------------------------------------
    // Check executable information

    if (ExecutableInfo.CodeSize == 0) return FALSE;

    //-------------------------------------
    // Lock access to kernel data

    LockMutex(MUTEX_KERNEL, INFINITY);

    //-------------------------------------
    // Allocate a new process structure

#ifdef __DEBUG__
    KernelPrint("CreateProcess() : Allocating process...\n");
#endif

    Process = NewProcess();
    if (Process == NULL) goto Out;

    StringCopy(Process->FileName, Info->FileName);

    CodeSize = ExecutableInfo.CodeSize;
    DataSize = ExecutableInfo.DataSize;
    HeapSize = ExecutableInfo.HeapRequested;
    StackSize = ExecutableInfo.StackRequested;

    if (StackSize < TASK_MINIMUM_STACK_SIZE) {
        StackSize = TASK_MINIMUM_STACK_SIZE;
    }

    //-------------------------------------
    // Compute addresses

    CodeBase = LA_USER;
    DataBase = CodeBase + CodeSize;

    while (DataBase & N_4KB_M1) DataBase++;

    HeapBase = DataBase + DataSize;

    while (HeapBase & N_4KB_M1) HeapBase++;

    //-------------------------------------
    // Compute total size

    TotalSize = (HeapBase + HeapSize) - LA_USER;

    //-------------------------------------
    // Allocate and setup the page directory

    Process->PageDirectory = AllocPageDirectory();

    //-------------------------------------

    FreezeScheduler();

    //-------------------------------------
    // We can use the new page directory from now on
    // and switch back to the previous when done

#ifdef __DEBUG__
    KernelPrint("CreateProcess() : Switching page directory...\n");
#endif

    PageDirectory = GetCurrentProcess()->PageDirectory;

    LoadPageDirectory(Process->PageDirectory);

    //-------------------------------------
    // Allocate enough memory for the code, data and heap

#ifdef __DEBUG__
    KernelPrint("CreateProcess() : VirtualAllocing process space...\n");
#endif

    VirtualAlloc(LA_USER, TotalSize, ALLOC_PAGES_COMMIT);

    //-------------------------------------
    // Open the executable file

    FileOpenInfo.Size = sizeof(FILEOPENINFO);
    FileOpenInfo.Name = Info->FileName;
    FileOpenInfo.Flags = FILE_OPEN_READ | FILE_OPEN_EXISTING;

    File = OpenFile(&FileOpenInfo);

    //-------------------------------------
    // Load executable image
    // For tests, image must be at LA_KERNEL

#ifdef __DEBUG__
    KernelPrint("CreateProcess() : Loading executable...\n");
#endif

    if (LoadExecutable_EXOS(File, &ExecutableInfo, (LINEAR)CodeBase,
                            (LINEAR)DataBase) == FALSE) {
#ifdef __DEBUG__
        KernelPrint("CreateProcess() : Load failed !\n");
#endif

        VirtualFree(LA_USER, TotalSize);
        LoadPageDirectory(PageDirectory);
        UnfreezeScheduler();
        CloseFile(File);
        goto Out;
    }

    CloseFile(File);

    //-------------------------------------
    // Initialize the heap

    Process->HeapBase = HeapBase;
    Process->HeapSize = HeapSize;

    MemorySet((LPVOID)Process->HeapBase, 0, Process->HeapSize);

    *((U32*)Process->HeapBase) = ID_HEAP;

    //-------------------------------------
    // Create the initial task

#ifdef __DEBUG__
    KernelPrint("CreateProcess() : Creating initial task...\n");
#endif

    // TaskInfo.Func      = (TASKFUNC) LA_USER;
    TaskInfo.Parameter = NULL;
    TaskInfo.StackSize = StackSize;
    TaskInfo.Priority = TASK_PRIORITY_MEDIUM;
    TaskInfo.Flags = TASK_CREATE_SUSPENDED;

    TaskInfo.Func = (TASKFUNC)(CodeBase + (ExecutableInfo.EntryPoint -
                                           ExecutableInfo.CodeBase));

    Task = CreateTask(Process, &TaskInfo);

    //-------------------------------------
    // Switch back to our page directory

#ifdef __DEBUG__
    KernelPrint("CreateProcess() : Switching page directory...\n");
#endif

    LoadPageDirectory(PageDirectory);

    //-------------------------------------

    UnfreezeScheduler();

    //-------------------------------------
    // Add the new process to the kernel's process list

    ListAddItem(Kernel.Process, Process);

    //-------------------------------------
    // Add initial task to the scheduler's queue

    AddTaskToQueue(Task);

    Result = TRUE;

Out:

    Info->Process = (HANDLE)Process;
    Info->Task = (HANDLE)Task;

    //-------------------------------------
    // Release access to kernel data

    UnlockMutex(MUTEX_KERNEL);

#ifdef __DEBUG__
    KernelPrint("Exiting CreateProcess : Result = %d\n", Result);
#endif

    return Result;
}

/***************************************************************************/

LINEAR GetProcessHeap(LPPROCESS Process) {
    LINEAR HeapBase = NULL;

    if (Process == NULL) Process = GetCurrentProcess();

    LockMutex(&(Process->Mutex), INFINITY);

    HeapBase = Process->HeapBase;

    UnlockMutex(&(Process->Mutex));

    return HeapBase;
}

/***************************************************************************/

void DumpProcess(LPPROCESS Process) {
    if (Process == NULL) return;

    LockMutex(&(Process->Mutex), INFINITY);

    KernelPrint(TEXT("Address        : %p\n"), Process);
    KernelPrint(TEXT("References     : %d\n"), Process->References);
    KernelPrint(TEXT("Parent         : %p\n"), Process->Parent);
    KernelPrint(TEXT("Privilege      : %d\n"), Process->Privilege);
    KernelPrint(TEXT("Page directory : %p\n"), Process->PageDirectory);
    KernelPrint(TEXT("File name      : %s\n"), Process->FileName);
    KernelPrint(TEXT("Heap base      : %p\n"), Process->HeapBase);
    KernelPrint(TEXT("Heap size      : %d\n"), Process->HeapSize);

    UnlockMutex(&(Process->Mutex));
}

/***************************************************************************/

void InitSecurity(LPSECURITY This) {
    if (This == NULL) return;

    This->ID = ID_SECURITY;
    This->References = 1;
    This->Next = NULL;
    This->Prev = NULL;
    This->Group = 0;
    This->User = 0;
    This->Permissions = PERMISSION_NONE;
}

/***************************************************************************/
