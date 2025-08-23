
/***************************************************************************\

    EXOS Kernel
    Copyright (c) 1999-2025 Jango73
    All rights reserved

\***************************************************************************/

#include "../include/Process.h"

#include "../include/Console.h"
#include "../include/File.h"
#include "../include/Kernel.h"
#include "../include/Log.h"

/***************************************************************************/

PROCESS KernelProcess = {
    .ID = ID_PROCESS,  // ID
    .References = 1,           // References
    .Next = NULL,
    .Prev = NULL,              // Next, previous
    .Mutex = EMPTY_MUTEX,       // Mutex
    .HeapMutex = EMPTY_MUTEX,       // Heap mutex
    .Security = EMPTY_SECURITY,    // Security
    .Desktop = NULL,              // Desktop
    .Parent = NULL,              // Parent
    .Privilege = PRIVILEGE_KERNEL,  // Privilege
    .PageDirectory = 0,            // Page directory
    .HeapBase = 0,                 // Heap base
    .HeapSize = 0,                 // Heap size
    .FileName = "EXOS",            // File name
    .CommandLine = "",                // Command line
    .Objects = NULL               // Objects
};

/***************************************************************************/

void InitializeKernelProcess(void) {
    TASKINFO TaskInfo;

    KernelProcess.PageDirectory = GetPageDirectory();
    KernelProcess.HeapSize = N_1MB;

    KernelLogText(LOG_DEBUG, TEXT("[InitializeKernelProcess] Memory : %X"), KernelStartup.MemorySize);
    KernelLogText(LOG_DEBUG, TEXT("[InitializeKernelProcess] Pages : %X"), KernelStartup.PageCount);

    LINEAR HeapBase = AllocRegion(0, 0, KernelProcess.HeapSize, ALLOC_PAGES_COMMIT | ALLOC_PAGES_READWRITE);

    KernelLogText(LOG_DEBUG, TEXT("[InitializeKernelProcess] HeapBase : %X"), HeapBase);

    if (!HeapBase) {
        ClearConsole();

        // Wait forever
        DO_THE_SLEEPING_BEAUTY;
    }

    KernelProcess.HeapBase = (LINEAR)HeapBase;
    MemorySet((LPVOID)KernelProcess.HeapBase, 0, sizeof(HEAPCONTROLBLOCK));

    ((LPHEAPCONTROLBLOCK)KernelProcess.HeapBase)->ID = ID_HEAP;

    TaskInfo.Header.Size = sizeof(TASKINFO);
    TaskInfo.Header.Version = EXOS_ABI_VERSION;
    TaskInfo.Header.Flags = 0;
    TaskInfo.Func = InitializeKernel;
    TaskInfo.StackSize = TASK_MINIMUM_STACK_SIZE;
    TaskInfo.Priority = TASK_PRIORITY_LOWEST;
    TaskInfo.Flags = 0;

    LPTASK KernelTask = CreateTask(&KernelProcess, &TaskInfo);

    if (KernelTask == NULL) {
        KernelLogText(LOG_DEBUG, TEXT("Could not create kernel task, halting."));
        DO_THE_SLEEPING_BEAUTY;
    }

    KernelTask->Type = TASK_TYPE_KERNEL_MAIN;
    MainDesktopWindow.Task = KernelTask;
    MainDesktop.Task = KernelTask;

    LoadInitialTaskRegister(KernelTask->Selector);
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

    KernelLogText(LOG_DEBUG, TEXT("Entering GetExecutableInfo_EXOS\n"));

    if (File == NULL) return FALSE;
    if (Info == NULL) return FALSE;

    FileOperation.Header.Size = sizeof(FILEOPERATION);
    FileOperation.Header.Version = EXOS_ABI_VERSION;
    FileOperation.Header.Flags = 0;
    FileOperation.File = (HANDLE)File;

    //-------------------------------------
    // Read the header

    FileOperation.NumBytes = sizeof(EXOSHEADER);
    FileOperation.Buffer = (LPVOID)&Header;
    BytesRead = ReadFile(&FileOperation);

    if (Header.Signature != EXOS_SIGNATURE) {
        KernelLogText(LOG_DEBUG, TEXT("GetExecutableInfo_EXOS() : Bad signature (%X)\n"), Header.Signature);

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

    KernelLogText(LOG_DEBUG, TEXT("Exiting GetExecutableInfo_EXOS (Success)\n"));

    return TRUE;

Out_Error:

    KernelLogText(LOG_DEBUG, TEXT("Exiting GetExecutableInfo_EXOS (Error)\n"));

    return FALSE;
}

/***************************************************************************/

BOOL LoadExecutable_EXOS(LPFILE File, LPEXECUTABLEINFO Info, LINEAR CodeBase, LINEAR DataBase) {
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

    KernelLogText(LOG_DEBUG, TEXT("Entering LoadExecutable_EXOS\n"));

    if (File == NULL) return FALSE;

    FileOperation.Header.Size = sizeof(FILEOPERATION);
    FileOperation.Header.Version = EXOS_ABI_VERSION;
    FileOperation.Header.Flags = 0;
    FileOperation.File = (HANDLE)File;

    CodeRead = 0;
    DataRead = 0;

    CodeOffset = CodeBase - Info->CodeBase;
    DataOffset = DataBase - Info->DataBase;

    KernelLogText(LOG_DEBUG, TEXT("LoadExecutable_EXOS() : CodeBase = %X\n"), CodeBase);
    KernelLogText(LOG_DEBUG, TEXT("LoadExecutable_EXOS() : DataBase = %X\n"), DataBase);

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

            KernelLogText(LOG_DEBUG, TEXT("LoadExecutable_EXOS() : Reading code\n"));

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

            KernelLogText(LOG_DEBUG, TEXT("LoadExecutable_EXOS() : Reading data\n"));

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

            KernelLogText(LOG_DEBUG, TEXT("LoadExecutable_EXOS() : Reading relocations\n"));

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

    KernelLogText(LOG_DEBUG, TEXT("Exiting LoadExecutable_EXOS\n"));

    return TRUE;

Out_Error:

    KernelLogText(LOG_DEBUG, TEXT("Exiting LoadExecutable_EXOS\n"));

    return FALSE;
}

/***************************************************************************/

LPPROCESS NewProcess(void) {
    LPPROCESS This = NULL;

    KernelLogText(LOG_DEBUG, TEXT("Entering NewProcess\n"));

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

    KernelLogText(LOG_DEBUG, TEXT("Exiting NewProcess\n"));

    return This;
}

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

    KernelLogText(LOG_DEBUG, TEXT("Entering CreateProcess\n"));

    if (Info == NULL) return FALSE;

    TaskInfo.Header.Size = sizeof(TASKINFO);
    TaskInfo.Header.Version = EXOS_ABI_VERSION;
    TaskInfo.Header.Flags = 0;

    //-------------------------------------
    // Open the executable file

    KernelLogText(LOG_DEBUG, TEXT("CreateProcess() : Opening file %s\n"), Info->FileName);

    FileOpenInfo.Header.Size = sizeof(FILEOPENINFO);
    FileOpenInfo.Header.Version = EXOS_ABI_VERSION;
    FileOpenInfo.Header.Flags = 0;
    FileOpenInfo.Name = Info->FileName;
    FileOpenInfo.Flags = FILE_OPEN_READ | FILE_OPEN_EXISTING;

    File = OpenFile(&FileOpenInfo);

    if (File == NULL) return FALSE;

    //-------------------------------------
    // Read the size of the file

    FileSize = GetFileSize(File);

    if (FileSize == 0) return FALSE;

    KernelLogText(LOG_DEBUG, TEXT("CreateProcess() : File size %d\n"), FileSize);

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

    KernelLogText(LOG_DEBUG, TEXT("CreateProcess() : Allocating process\n"));

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

    KernelLogText(LOG_DEBUG, TEXT("CreateProcess() : Switching page directory\n"));

    PageDirectory = GetCurrentProcess()->PageDirectory;

    LoadPageDirectory(Process->PageDirectory);

    //-------------------------------------
    // Allocate enough memory for the code, data and heap

    KernelLogText(LOG_DEBUG, TEXT("CreateProcess() : AllocRegioning process space\n"));

    AllocRegion(LA_USER, 0, TotalSize, ALLOC_PAGES_COMMIT | ALLOC_PAGES_READWRITE);

    //-------------------------------------
    // Open the executable file

    FileOpenInfo.Header.Size = sizeof(FILEOPENINFO);
    FileOpenInfo.Header.Version = EXOS_ABI_VERSION;
    FileOpenInfo.Header.Flags = 0;
    FileOpenInfo.Name = Info->FileName;
    FileOpenInfo.Flags = FILE_OPEN_READ | FILE_OPEN_EXISTING;

    File = OpenFile(&FileOpenInfo);

    //-------------------------------------
    // Load executable image
    // For tests, image must be at LA_KERNEL

    KernelLogText(LOG_DEBUG, TEXT("CreateProcess() : Loading executable\n"));

    if (LoadExecutable_EXOS(File, &ExecutableInfo, (LINEAR)CodeBase, (LINEAR)DataBase) == FALSE) {
        KernelLogText(LOG_DEBUG, TEXT("CreateProcess() : Load failed !\n"));

        FreeRegion(LA_USER, TotalSize);
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

    KernelLogText(LOG_DEBUG, TEXT("CreateProcess() : Creating initial task\n"));

    // TaskInfo.Func      = (TASKFUNC) LA_USER;
    TaskInfo.Parameter = NULL;
    TaskInfo.StackSize = StackSize;
    TaskInfo.Priority = TASK_PRIORITY_MEDIUM;
    TaskInfo.Flags = TASK_CREATE_SUSPENDED;

    TaskInfo.Func = (TASKFUNC)(CodeBase + (ExecutableInfo.EntryPoint - ExecutableInfo.CodeBase));

    Task = CreateTask(Process, &TaskInfo);

    //-------------------------------------
    // Switch back to our page directory

    KernelLogText(LOG_DEBUG, TEXT("CreateProcess() : Switching page directory\n"));

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

    KernelLogText(LOG_DEBUG, TEXT("Exiting CreateProcess : Result = %d\n"), Result);

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

    KernelLogText(LOG_DEBUG, TEXT("Address        : %p\n"), Process);
    KernelLogText(LOG_DEBUG, TEXT("References     : %d\n"), Process->References);
    KernelLogText(LOG_DEBUG, TEXT("Parent         : %p\n"), Process->Parent);
    KernelLogText(LOG_DEBUG, TEXT("Privilege      : %d\n"), Process->Privilege);
    KernelLogText(LOG_DEBUG, TEXT("Page directory : %p\n"), Process->PageDirectory);
    KernelLogText(LOG_DEBUG, TEXT("File name      : %s\n"), Process->FileName);
    KernelLogText(LOG_DEBUG, TEXT("Heap base      : %p\n"), Process->HeapBase);
    KernelLogText(LOG_DEBUG, TEXT("Heap size      : %d\n"), Process->HeapSize);

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
