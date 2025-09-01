
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


    Process

\************************************************************************/
#include "../include/Process.h"

#include "../include/Console.h"
#include "../include/Executable.h"
#include "../include/File.h"
#include "../include/Kernel.h"
#include "../include/Log.h"

/***************************************************************************/

PROCESS KernelProcess = {
    .ID = ID_PROCESS,  // ID
    .References = 1,   // References
    .Next = NULL,
    .Prev = NULL,                   // Next, previous
    .Mutex = EMPTY_MUTEX,           // Mutex
    .HeapMutex = EMPTY_MUTEX,       // Heap mutex
    .Security = EMPTY_SECURITY,     // Security
    .Desktop = NULL,                // Desktop
    .Parent = NULL,                 // Parent
    .Privilege = PRIVILEGE_KERNEL,  // Privilege
    .PageDirectory = 0,             // Page directory
    .HeapBase = 0,                  // Heap base
    .HeapSize = 0,                  // Heap size
    .FileName = "EXOS",             // File name
    .CommandLine = "",              // Command line
    .Objects = NULL                 // Objects
};

/***************************************************************************/

/**
 * @brief Initialize the kernel process and main task.
 *
 * Prepare the kernel heap, set up the kernel process fields and create the
 * primary kernel task.
 */
void InitializeKernelProcess(void) {
    TASKINFO TaskInfo;

    KernelLogText(LOG_DEBUG, TEXT("[InitializeKernelProcess] Enter"));

    KernelProcess.PageDirectory = GetPageDirectory();
    KernelProcess.HeapSize = N_1MB;

    KernelLogText(LOG_DEBUG, TEXT("[InitializeKernelProcess] Memory : %X"), KernelStartup.MemorySize);
    KernelLogText(LOG_DEBUG, TEXT("[InitializeKernelProcess] Pages : %X"), KernelStartup.PageCount);

    LINEAR HeapBase = AllocRegion(
        LA_KERNEL, 0, KernelProcess.HeapSize, ALLOC_PAGES_COMMIT | ALLOC_PAGES_READWRITE | ALLOC_PAGES_AT_OR_OVER);

    KernelLogText(LOG_DEBUG, TEXT("[InitializeKernelProcess] HeapBase : %X"), HeapBase);

    if (HeapBase == NULL) {
        KernelLogText(LOG_DEBUG, TEXT("[InitializeKernelProcess] Could not create kernel heap, halting."));
        DO_THE_SLEEPING_BEAUTY;
    }

    KernelProcess.HeapBase = (LINEAR)HeapBase;
    MemorySet((LPVOID)KernelProcess.HeapBase, 0, sizeof(HEAPCONTROLBLOCK));

    ((LPHEAPCONTROLBLOCK)KernelProcess.HeapBase)->ID = ID_HEAP;

    TaskInfo.Header.Size = sizeof(TASKINFO);
    TaskInfo.Header.Version = EXOS_ABI_VERSION;
    TaskInfo.Header.Flags = 0;
    TaskInfo.Func = (TASKFUNC)InitializeKernel;
    TaskInfo.StackSize = TASK_MINIMUM_STACK_SIZE;
    TaskInfo.Priority = TASK_PRIORITY_LOWEST;
    TaskInfo.Flags = TASK_CREATE_MAIN;

    LPTASK KernelTask = CreateTask(&KernelProcess, &TaskInfo);

    if (KernelTask == NULL) {
        KernelLogText(LOG_DEBUG, TEXT("Could not create kernel task, halting."));
        DO_THE_SLEEPING_BEAUTY;
    }

    KernelTask->Type = TASK_TYPE_KERNEL_MAIN;
    MainDesktopWindow.Task = KernelTask;
    MainDesktop.Task = KernelTask;

    KernelLogText(LOG_DEBUG, TEXT("[InitializeKernelProcess] Loading TR"));

    LoadInitialTaskRegister(SELECTOR_TSS);

    KernelLogText(LOG_DEBUG, TEXT("[InitializeKernelProcess] Exit"));
}

/***************************************************************************/

/**
 * @brief Allocate and initialize a new user process structure.
 *
 * @return Pointer to the new PROCESS or NULL on failure.
 */
LPPROCESS NewProcess(void) {
    LPPROCESS This = NULL;

    KernelLogText(LOG_DEBUG, TEXT("[NewProcess] Enter\n"));

    This = (LPPROCESS)HeapAlloc(sizeof(PROCESS));

    if (This == NULL) return NULL;

    MemorySet(This, 0, sizeof(PROCESS));

    This->ID = ID_PROCESS;
    This->References = 1;
    This->Desktop = (LPDESKTOP)Kernel.Desktop->First;
    This->Parent = GetCurrentProcess();
    This->Privilege = PRIVILEGE_USER;

    //-------------------------------------
    // Initialize the process' mutexs

    InitMutex(&(This->Mutex));
    InitMutex(&(This->HeapMutex));

    //-------------------------------------
    // Initialize the process' security

    InitSecurity(&(This->Security));

    KernelLogText(LOG_DEBUG, TEXT("[NewProcess] Exit\n"));

    return This;
}

/***************************************************************************/

/**
 * @brief Create a new process from an executable file.
 *
 * @param Info Pointer to a PROCESSINFO describing the executable.
 * @return TRUE on success, FALSE on failure.
 */
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

    if (GetExecutableInfo(File, &ExecutableInfo) == FALSE) {
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

    EXECUTABLELOAD LoadInfo;
    LoadInfo.File = File;
    LoadInfo.Info = &ExecutableInfo;
    LoadInfo.CodeBase = (LINEAR)CodeBase;
    LoadInfo.DataBase = (LINEAR)DataBase;

    if (LoadExecutable(&LoadInfo) == FALSE) {
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

/**
 * @brief Create a new process using only a file name and command line.
 *
 * @param FileName Name of the executable file to run.
 * @param CommandLine Command line passed to the process or NULL.
 * @return TRUE on success, FALSE otherwise.
 */
BOOL Spawn(LPCSTR FileName, LPCSTR CommandLine) {
    KernelLogText(LOG_WARNING, TEXT("[Spawn] Lunching : %s"), FileName);

    PROCESSINFO ProcessInfo;

    ProcessInfo.Header.Size = sizeof(PROCESSINFO);
    ProcessInfo.Header.Version = EXOS_ABI_VERSION;
    ProcessInfo.Header.Flags = 0;
    ProcessInfo.Flags = 0;
    ProcessInfo.FileName = FileName;
    ProcessInfo.CommandLine = CommandLine;
    ProcessInfo.StdOut = NULL;
    ProcessInfo.StdIn = NULL;
    ProcessInfo.StdErr = NULL;

    return CreateProcess(&ProcessInfo);
}

/***************************************************************************/

/**
 * @brief Retrieve the heap base address of a process.
 *
 * @param Process Process to inspect, or NULL for the current process.
 * @return Linear address of the process heap.
 */
LINEAR GetProcessHeap(LPPROCESS Process) {
    LINEAR HeapBase = NULL;

    if (Process == NULL) Process = GetCurrentProcess();

    LockMutex(&(Process->Mutex), INFINITY);

    HeapBase = Process->HeapBase;

    UnlockMutex(&(Process->Mutex));

    return HeapBase;
}

/***************************************************************************/

/**
 * @brief Output process information to the kernel log.
 *
 * @param Process Process to dump. Nothing is logged if NULL.
 */
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

/**
 * @brief Initialize a SECURITY structure.
 *
 * @param This SECURITY structure to initialize.
 */
void InitSecurity(LPSECURITY This) {
    if (This == NULL) return;

    This->ID = ID_SECURITY;
    This->References = 1;
    This->Next = NULL;
    This->Prev = NULL;
    This->Owner = U64_Make(0, 0);
    This->UserPermissionCount = 0;
    This->DefaultPermissions = PERMISSION_NONE;
}

/***************************************************************************/
