
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
#include "../include/binary/Binary-EXOS.h"
#include "../include/binary/Binary-ELF.h"

#define BINARY_TYPE_EXOS 1
#define BINARY_TYPE_ELF  2

/***************************************************************************/

PROCESS KernelProcess = {
    ID_PROCESS,  // ID
    1,           // References
    NULL,
    NULL,              // Next, previous
    EMPTY_MUTEX,       // Mutex
    EMPTY_MUTEX,       // Heap mutex
    EMPTY_SECURITY,    // Security
    NULL,              // Desktop
    NULL,              // Parent
    PRIVILEGE_KERNEL,  // Privilege
    PA_PGD,            // Page directory
    0,                 // Heap base
    0,                 // Heap size
    "EXOS",            // File name
    "",                // Command line
    NULL               // Objects
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
    FILEOPERATION FileOperation;
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
    U32 BinaryType = 0;
    U32 Signature = 0;
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
    // Detect binary type

    FileOperation.Size = sizeof(FILEOPERATION);
    FileOperation.File = (HANDLE)File;
    FileOperation.NumBytes = sizeof(U32);
    FileOperation.Buffer = (LPVOID)&Signature;
    ReadFile(&FileOperation);

    if (Signature == EXOS_SIGNATURE) {
        BinaryType = BINARY_TYPE_EXOS;
    } else if (Signature == ELF_SIGNATURE) {
        BinaryType = BINARY_TYPE_ELF;
    } else {
        CloseFile(File);
        return FALSE;
    }

    CloseFile(File);

    //-------------------------------------
    // Get executable information

    File = OpenFile(&FileOpenInfo);
    if (File == NULL) return FALSE;

    if (BinaryType == BINARY_TYPE_EXOS) {
        if (GetExecutableInfo_EXOS(File, &ExecutableInfo) == FALSE) {
            CloseFile(File);
            return FALSE;
        }
    } else {
        if (GetExecutableInfo_ELF(File, &ExecutableInfo) == FALSE) {
            CloseFile(File);
            return FALSE;
        }
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

    BOOL LoadResult;
    if (BinaryType == BINARY_TYPE_EXOS) {
        LoadResult = LoadExecutable_EXOS(File, &ExecutableInfo, (LINEAR)CodeBase,
                                         (LINEAR)DataBase);
    } else {
        LoadResult = LoadExecutable_ELF(File, &ExecutableInfo, (LINEAR)CodeBase,
                                        (LINEAR)DataBase);
    }

    if (LoadResult == FALSE) {
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
