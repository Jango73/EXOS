
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


    Process manager

\************************************************************************/

#include "../include/Process.h"

#include "../include/Console.h"
#include "../include/Executable.h"
#include "../include/File.h"
#include "../include/Kernel.h"
#include "../include/List.h"
#include "../include/Log.h"
#include "../include/String.h"
#include "../include/StackTrace.h"

/***************************************************************************/

PROCESS SECTION(".data") KernelProcess = {
    .TypeID = KOID_PROCESS,  // ID
    .References = 1,   // References
    .OwnerProcess = NULL, // OwnerProcess (from LISTNODE_FIELDS)
    .Next = NULL,
    .Prev = NULL,                   // Next, previous
    .Mutex = EMPTY_MUTEX,           // Mutex
    .HeapMutex = EMPTY_MUTEX,       // Heap mutex
    .Security = EMPTY_SECURITY,     // Security
    .Desktop = NULL,                // Desktop
    .Privilege = PRIVILEGE_KERNEL,  // Privilege
    .Status = PROCESS_STATUS_ALIVE, // Status
    .Flags = PROCESS_CREATE_KILL_CHILDREN_ON_DEATH, // Flags
    .PageDirectory = 0,             // Page directory
    .HeapBase = 0,                  // Heap base
    .HeapSize = 0,                  // Heap size
    .FileName = "EXOS",             // File name
    .CommandLine = "",              // Command line
    .WorkFolder = ROOT,             // Working directory
    .TaskCount = 0                  // Task count (will be incremented by CreateTask)
};

/***************************************************************************/

/**
 * @brief Initialize the kernel process and main task.
 *
 * Prepare the kernel heap, set up the kernel process fields and create the
 * primary kernel task.
 */
void InitializeKernelProcess(void) {
    TRACED_FUNCTION;

    TASKINFO TaskInfo;

    DEBUG(TEXT("[InitializeKernelProcess] Enter"));

    KernelProcess.PageDirectory = GetPageDirectory();
    KernelProcess.HeapSize = N_1MB;

    DEBUG(TEXT("[InitializeKernelProcess] Memory : %x"), KernelStartup.MemorySize);
    DEBUG(TEXT("[InitializeKernelProcess] Pages : %x"), KernelStartup.PageCount);

    LINEAR HeapBase = AllocKernelRegion(0, KernelProcess.HeapSize, ALLOC_PAGES_COMMIT | ALLOC_PAGES_READWRITE);

    DEBUG(TEXT("[InitializeKernelProcess] HeapBase : %x"), HeapBase);

    if (HeapBase == NULL) {
        DEBUG(TEXT("[InitializeKernelProcess] Could not create kernel heap, halting."));
        DO_THE_SLEEPING_BEAUTY;
    }

    KernelProcess.HeapBase = (LINEAR)HeapBase;

    HeapInit(KernelProcess.HeapBase, KernelProcess.HeapSize);

    TaskInfo.Header.Size = sizeof(TASKINFO);
    TaskInfo.Header.Version = EXOS_ABI_VERSION;
    TaskInfo.Header.Flags = 0;
    TaskInfo.Func = (TASKFUNC)InitializeKernel;
    TaskInfo.StackSize = TASK_MINIMUM_STACK_SIZE;
    TaskInfo.Priority = TASK_PRIORITY_LOWEST;
    TaskInfo.Flags = TASK_CREATE_MAIN_KERNEL;
    StringCopy(TaskInfo.Name, TEXT("KernelMain"));

    LPTASK KernelTask = CreateTask(&KernelProcess, &TaskInfo);

    if (KernelTask == NULL) {
        DEBUG(TEXT("Could not create kernel task, halting."));
        DO_THE_SLEEPING_BEAUTY;
    }

    DEBUG(TEXT("Kernel main task = %x (%s)"), KernelTask, KernelTask->Name);

    KernelTask->Type = TASK_TYPE_KERNEL_MAIN;
    MainDesktopWindow.Task = KernelTask;
    MainDesktop.Task = KernelTask;

    DEBUG(TEXT("[InitializeKernelProcess] Loading TR"));

    LoadInitialTaskRegister(SELECTOR_TSS);

    DEBUG(TEXT("[InitializeKernelProcess] Exit"));

    TRACED_EPILOGUE("InitializeKernelProcess");
}

/***************************************************************************/

/**
 * @brief Allocate and initialize a new user process structure.
 *
 * @return Pointer to the new PROCESS or NULL on failure.
 */
LPPROCESS NewProcess(void) {
    TRACED_FUNCTION;

    LPPROCESS This = NULL;

    DEBUG(TEXT("[NewProcess] Enter"));

    This = (LPPROCESS)CreateKernelObject(sizeof(PROCESS), KOID_PROCESS);

    if (This == NULL) {
        TRACED_EPILOGUE("NewProcess");
        return NULL;
    }

    // Zero out non-LISTNODE_FIELDS (LISTNODE_FIELDS already initialized by CreateKernelObject)
    MemorySet(&This->Mutex, 0, sizeof(PROCESS) - sizeof(LISTNODE));

    This->Desktop = (LPDESKTOP)Kernel.Desktop->First;
    This->Privilege = PRIVILEGE_USER;
    This->Status = PROCESS_STATUS_ALIVE;
    This->Flags = 0; // Will be set by CreateProcess
    This->TaskCount = 0;
    This->Session = NULL;

    // Inherit session from parent process
    SAFE_USE_VALID_ID(This->OwnerProcess, KOID_PROCESS) {
        This->Session = This->OwnerProcess->Session;
    }

    //-------------------------------------
    // Initialize the process' mutex

    InitMutex(&(This->Mutex));
    InitMutex(&(This->HeapMutex));

    //-------------------------------------
    // Initialize the process' security

    InitSecurity(&(This->Security));

    DEBUG(TEXT("[NewProcess] Exit"));

    TRACED_EPILOGUE("NewProcess");
    return This;
}

/***************************************************************************/

/**
 * @brief Actually delete a single process (the original DeleteProcess logic).
 *
 * @param This The process to delete.
 */
void DeleteProcessCommit(LPPROCESS This) {
    TRACED_FUNCTION;

    SAFE_USE_VALID_ID(This, KOID_PROCESS) {
        if (This == &KernelProcess) {
            ERROR(TEXT("[DeleteProcessCommit] Cannot delete kernel process"));
            TRACED_EPILOGUE("DeleteProcessCommit");
            return;
        }

        DEBUG(TEXT("[DeleteProcessCommit] Deleting process %s (TaskCount=%d)"), This->FileName, This->TaskCount);

        // Free page directory if allocated
        // TODO : FREE ALL PD PAGES
        if (This->PageDirectory != 0) {
            DEBUG(TEXT("[DeleteProcessCommit] Freeing page directory %x"), This->PageDirectory);
            FreePhysicalPage(This->PageDirectory);
        }

        // Free process heap if allocated
        if (This->HeapBase != 0 && This->HeapSize != 0) {
            DEBUG(TEXT("[DeleteProcessCommit] Freeing process heap base=%x size=%x"), This->HeapBase,
                This->HeapSize);
            FreeRegion(This->HeapBase, This->HeapSize);
        }

        ReleaseKernelObject(This);

        DEBUG(TEXT("[DeleteProcessCommit] Process deleted"));
    }

    TRACED_EPILOGUE("DeleteProcessCommit");
}

/***************************************************************************/

void KillProcess(LPPROCESS This) {
    TRACED_FUNCTION;

    SAFE_USE_VALID_ID(This, KOID_PROCESS) {
        if (This == &KernelProcess) {
            ERROR(TEXT("[KillProcess] Cannot delete kernel process"));
            TRACED_EPILOGUE("KillProcess");
            return;
        }

        DEBUG(TEXT("[KillProcess] Killing process %s and all its children"), This->FileName);

        // Lock the process list early and keep it locked throughout the entire operation
        LockMutex(MUTEX_PROCESS, INFINITY);

        // Create a temporary list to collect all child processes
        LPLIST ChildProcesses = NewList(NULL, KernelHeapAlloc, KernelHeapFree);
        if (ChildProcesses == NULL) {
            ERROR(TEXT("[KillProcess] Failed to create temporary list"));
            UnlockMutex(MUTEX_PROCESS);
            TRACED_EPILOGUE("KillProcess");
            return;
        }

        // Find all child processes recursively
        BOOL FoundChildren = TRUE;
        LPLIST ProcessesToCheck = NewList(NULL, KernelHeapAlloc, KernelHeapFree);
        ListAddItem(ProcessesToCheck, This);

        while (FoundChildren) {
            FoundChildren = FALSE;
            LPPROCESS Current = (LPPROCESS)Kernel.Process->First;

            while (Current != NULL) {
                SAFE_USE_VALID_ID(Current, KOID_PROCESS) {
                    // Check if this process has a parent in our check list
                    for (U32 i = 0; i < ListGetSize(ProcessesToCheck); i++) {
                        LPPROCESS ParentToCheck = (LPPROCESS)ListGetItem(ProcessesToCheck, i);

                        if (Current->OwnerProcess == ParentToCheck && Current != This) {
                            // Check if this child is not already in the list
                            BOOL AlreadyInList = FALSE;

                            for (U32 j = 0; j < ListGetSize(ChildProcesses); j++) {
                                if (ListGetItem(ChildProcesses, j) == Current) {
                                    AlreadyInList = TRUE;
                                    break;
                                }
                            }

                            if (!AlreadyInList) {
                                ListAddItem(ChildProcesses, Current);
                                ListAddItem(ProcessesToCheck, Current);
                                FoundChildren = TRUE;
                                DEBUG(TEXT("[KillProcess] Found child process %s"), Current->FileName);
                            }
                            break;
                        }
                    }
                }
                Current = (LPPROCESS)Current->Next;
            }
        }

        DeleteList(ProcessesToCheck);

        // Process child processes according to parent's policy
        U32 ChildCount = ListGetSize(ChildProcesses);
        DEBUG(TEXT("[KillProcess] Processing %d child processes"), ChildCount);

        if (This->Flags & PROCESS_CREATE_KILL_CHILDREN_ON_DEATH) {
            DEBUG(TEXT("[KillProcess] Policy: KILL_CHILDREN_ON_DEATH - killing all children"));

            for (U32 i = 0; i < ChildCount; i++) {
                LPPROCESS ChildProcess = (LPPROCESS)ListGetItem(ChildProcesses, i);
                SAFE_USE_VALID_ID(ChildProcess, KOID_PROCESS) {
                    DEBUG(TEXT("[KillProcess] Killing tasks of child process %s"), ChildProcess->FileName);

                    // Kill all tasks of this child process
                    LPTASK Task = (LPTASK)Kernel.Task->First;
                    while (Task != NULL) {
                        LPTASK NextTask = (LPTASK)Task->Next;
                        SAFE_USE_VALID_ID(Task, KOID_TASK) {
                            if (Task->Process == ChildProcess) {
                                DEBUG(TEXT("[KillProcess] Killing task %s"), Task->Name);
                                KillTask(Task);
                            }
                        }
                        Task = NextTask;
                    }

                    // Mark the child process as DEAD
                    SetProcessStatus(ChildProcess, PROCESS_STATUS_DEAD);
                }
            }
        } else {
            DEBUG(TEXT("[KillProcess] Policy: ORPHAN_CHILDREN - detaching children from parent"));

            for (U32 i = 0; i < ChildCount; i++) {
                LPPROCESS ChildProcess = (LPPROCESS)ListGetItem(ChildProcesses, i);
                SAFE_USE_VALID_ID(ChildProcess, KOID_PROCESS) {
                    // Detach child from parent (make it orphan)
                    ChildProcess->OwnerProcess = NULL;
                    DEBUG(TEXT("[KillProcess] Detached child process %s from parent"), ChildProcess->FileName);
                }
            }
        }

        // Clean up the temporary list
        DeleteList(ChildProcesses);

        // Kill all tasks of the target process itself
        DEBUG(TEXT("[KillProcess] Killing tasks of target process %s"), This->FileName);

        LPTASK Task = (LPTASK)Kernel.Task->First;
        while (Task != NULL) {
            LPTASK NextTask = (LPTASK)Task->Next;
            SAFE_USE_VALID_ID(Task, KOID_TASK) {
                if (Task->Process == This) {
                    DEBUG(TEXT("[KillProcess] Killing task %s"), Task->Name);
                    KillTask(Task);
                }
            }
            Task = NextTask;
        }

        // Mark the target process as DEAD
        SetProcessStatus(This, PROCESS_STATUS_DEAD);

        // Finally unlock the process mutex
        UnlockMutex(MUTEX_PROCESS);

        DEBUG(TEXT("[KillProcess] Process and children marked for deletion"));
    }

    TRACED_EPILOGUE("KillProcess");
}

/************************************************************************/

/**
 * @brief Create a new process from an executable file.
 *
 * @param Info Pointer to a PROCESSINFO describing the executable.
 * @return TRUE on success, FALSE on failure.
 */
BOOL CreateProcess(LPPROCESSINFO Info) {
    TRACED_FUNCTION;

    EXECUTABLEINFO ExecutableInfo;
    TASKINFO TaskInfo;
    FILEOPENINFO FileOpenInfo;
    LPPROCESS Process = NULL;
    LPPROCESS ParentProcess = NULL;
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

    DEBUG(TEXT("[CreateProcess] Enter"));

    if (Info == NULL) {
        TRACED_EPILOGUE("CreateProcess");
        return FALSE;
    }

    MemorySet(&TaskInfo, 0, sizeof(TaskInfo));
    TaskInfo.Header.Size = sizeof(TASKINFO);
    TaskInfo.Header.Version = EXOS_ABI_VERSION;
    TaskInfo.Header.Flags = 0;

    StringCopy(TaskInfo.Name, TEXT("UserMain"));

    //-------------------------------------
    // Extract filename from CommandLine

    STR FileName[MAX_PATH_NAME];
    LPCSTR CommandLineStart;
    INT i;

    // Find the first space or end of string to extract filename
    for (i = 0; i < MAX_PATH_NAME - 1 && Info->CommandLine[i] != STR_NULL && Info->CommandLine[i] != STR_SPACE; i++) {
        FileName[i] = Info->CommandLine[i];
    }
    FileName[i] = STR_NULL;

    // CommandLine starts after the filename and any spaces
    CommandLineStart = Info->CommandLine;
    while (*CommandLineStart != STR_NULL && *CommandLineStart != STR_SPACE) CommandLineStart++;
    while (*CommandLineStart == STR_SPACE) CommandLineStart++;

    //-------------------------------------
    // Open the executable file

    DEBUG(TEXT("[CreateProcess] : Opening file %s"), FileName);

    FileOpenInfo.Header.Size = sizeof(FILEOPENINFO);
    FileOpenInfo.Header.Version = EXOS_ABI_VERSION;
    FileOpenInfo.Header.Flags = 0;
    FileOpenInfo.Name = FileName;
    FileOpenInfo.Flags = FILE_OPEN_READ | FILE_OPEN_EXISTING;

    File = OpenFile(&FileOpenInfo);

    if (File == NULL) {
        TRACED_EPILOGUE("CreateProcess");
        return FALSE;
    }

    //-------------------------------------
    // Read the size of the file

    FileSize = GetFileSize(File);

    if (FileSize == 0) {
        TRACED_EPILOGUE("CreateProcess");
        return FALSE;
    }

    DEBUG(TEXT("[CreateProcess] : File size %d"), FileSize);

    //-------------------------------------
    // Get executable information

    if (GetExecutableInfo(File, &ExecutableInfo) == FALSE) {
        TRACED_EPILOGUE("CreateProcess");
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

    DEBUG(TEXT("[CreateProcess] : Allocating process"));

    Process = NewProcess();
    if (Process == NULL) goto Out;

    StringCopy(Process->FileName, FileName);

    // Initialize CommandLine (could be empty if not provided)
    if (StringEmpty(Info->CommandLine) == FALSE) {
        StringCopy(Process->CommandLine, Info->CommandLine);
    } else {
        StringClear(Process->CommandLine);
    }

    // Initialize WorkFolder from PROCESSINFO or inherit from parent
    if (!StringEmpty(Info->WorkFolder)) {
        StringCopy(Process->WorkFolder, Info->WorkFolder);
    } else {
        ParentProcess = GetCurrentProcess();

        SAFE_USE_VALID_ID(ParentProcess, KOID_PROCESS) {
            StringCopy(Process->WorkFolder, ParentProcess->WorkFolder);
        } else {
            StringCopy(Process->WorkFolder, ROOT);
        }
    }

    // Update returned PROCESSINFO with effective WorkFolder
    StringCopy(Info->WorkFolder, Process->WorkFolder);

    // Copy process creation flags
    Process->Flags = Info->Flags;

    CodeSize = ExecutableInfo.CodeSize;
    DataSize = ExecutableInfo.DataSize;
    HeapSize = ExecutableInfo.HeapRequested;
    StackSize = ExecutableInfo.StackRequested;

    if (HeapSize < N_64KB) {
        HeapSize = N_64KB;
    }

    if (StackSize < TASK_MINIMUM_STACK_SIZE) {
        StackSize = TASK_MINIMUM_STACK_SIZE;
    }

    //-------------------------------------
    // Compute addresses

    CodeBase = VMA_USER;
    DataBase = CodeBase + CodeSize;

    while (DataBase & N_4KB_M1) DataBase++;  // Align 4K

    HeapBase = DataBase + DataSize;

    while (HeapBase & N_4KB_M1) HeapBase++;  // Align 4K

    //-------------------------------------
    // Compute total size

    TotalSize = (HeapBase + HeapSize) - VMA_USER;

    //-------------------------------------

    FreezeScheduler();

    //-------------------------------------
    // Allocate and setup the page directory

    Process->PageDirectory = AllocUserPageDirectory();

    if (Process->PageDirectory == NULL) {
        ERROR(TEXT("[CreateProcess] Failed to allocate page directory"));
        UnfreezeScheduler();
        CloseFile(File);
        goto Out;
    }

    DEBUG(TEXT("[CreateProcess] Page directory allocated at physical 0x%X"), Process->PageDirectory);

    //-------------------------------------
    // We can use the new page directory from now on
    // and switch back to the previous when done

    DEBUG(TEXT("[CreateProcess] Switching page directory to new process : %x"), Process->PageDirectory);

    PageDirectory = GetCurrentProcess()->PageDirectory;

    LoadPageDirectory(Process->PageDirectory);

    DEBUG(TEXT("[CreateProcess] Page directory switch successful"));

    //-------------------------------------
    // Allocate enough memory for the code, data and heap

    DEBUG(TEXT("[CreateProcess] Allocating process space"));

    if (AllocRegion(VMA_USER, 0, TotalSize, ALLOC_PAGES_COMMIT | ALLOC_PAGES_READWRITE) == NULL) {
        ERROR(TEXT("[CreateProcess] Failed to allocate process space"));
        LoadPageDirectory(PageDirectory);
        UnfreezeScheduler();
        CloseFile(File);
        goto Out;
    }

    //-------------------------------------
    // Open the executable file

    FileOpenInfo.Header.Size = sizeof(FILEOPENINFO);
    FileOpenInfo.Header.Version = EXOS_ABI_VERSION;
    FileOpenInfo.Header.Flags = 0;
    FileOpenInfo.Name = FileName;
    FileOpenInfo.Flags = FILE_OPEN_READ | FILE_OPEN_EXISTING;

    File = OpenFile(&FileOpenInfo);

    //-------------------------------------
    // Load executable image
    // For tests, image must be at VMA_KERNEL

    DEBUG(TEXT("[CreateProcess] Loading executable"));

    EXECUTABLELOAD LoadInfo;
    LoadInfo.File = File;
    LoadInfo.Info = &ExecutableInfo;
    LoadInfo.CodeBase = (LINEAR)CodeBase;
    LoadInfo.DataBase = (LINEAR)DataBase;

    if (LoadExecutable(&LoadInfo) == FALSE) {
        DEBUG(TEXT("[CreateProcess] Load failed !"));

        FreeRegion(VMA_USER, TotalSize);
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

    HeapInit(Process->HeapBase, Process->HeapSize);

    // HeapDump(KernelProcess.HeapBase, KernelProcess.HeapSize);
    // HeapDump(Process->HeapBase, Process->HeapSize);

    //-------------------------------------
    // Create the initial task

    DEBUG(TEXT("[CreateProcess] Creating initial task"));

    // TaskInfo.Func      = (TASKFUNC) VMA_USER;
    TaskInfo.Func = (TASKFUNC)(CodeBase + (ExecutableInfo.EntryPoint - ExecutableInfo.CodeBase));
    TaskInfo.Parameter = NULL;
    TaskInfo.StackSize = StackSize;
    TaskInfo.Priority = TASK_PRIORITY_MEDIUM;
    TaskInfo.Flags = TASK_CREATE_SUSPENDED;

    Task = CreateTask(Process, &TaskInfo);

    //-------------------------------------
    // Switch back to kernel page directory

    DEBUG(TEXT("[CreateProcess] Switching back page directory to %x"), PageDirectory);

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

    DEBUG(TEXT("[CreateProcess] Exit, Result = %d"), Result);

    TRACED_EPILOGUE("CreateProcess");
    return Result;
}

/***************************************************************************/

/**
 * @brief Create a new process using a full command line and wait for it to complete.
 *
 * @param CommandLine Full command line including executable name and arguments.
 * @param WorkFolder Working directory to use, or empty/NULL to inherit from parent.
 * @return The process exit code on success, MAX_U32 on failure.
 */
U32 Spawn(LPCSTR CommandLine, LPCSTR WorkFolder) {
    DEBUG(TEXT("[Spawn] Launching : %s"), CommandLine);

    PROCESSINFO ProcessInfo;
    WAITINFO WaitInfo;
    U32 Result;
    LPPROCESS ParentProcess = NULL;

    MemorySet(&ProcessInfo, 0, sizeof(PROCESSINFO));
    ProcessInfo.Header.Size = sizeof(PROCESSINFO);
    ProcessInfo.Header.Version = EXOS_ABI_VERSION;
    ProcessInfo.Header.Flags = 0;
    ProcessInfo.Flags = 0;
    ProcessInfo.StdOut = NULL;
    ProcessInfo.StdIn = NULL;
    ProcessInfo.StdErr = NULL;
    ProcessInfo.Process = NULL;

    StringCopy(ProcessInfo.CommandLine, CommandLine);

    if (StringEmpty(WorkFolder) == FALSE) {
        StringCopy(ProcessInfo.WorkFolder, WorkFolder);
    } else {
        ParentProcess = GetCurrentProcess();
        SAFE_USE_VALID_ID(ParentProcess, KOID_PROCESS) {
            StringCopy(ProcessInfo.WorkFolder, ParentProcess->WorkFolder);
        }
    }

    if (!CreateProcess(&ProcessInfo) || ProcessInfo.Process == NULL) {
        return MAX_U32;
    }

    // Wait for the process to complete
    WaitInfo.Header.Size = sizeof(WAITINFO);
    WaitInfo.Header.Version = EXOS_ABI_VERSION;
    WaitInfo.Header.Flags = 0;
    WaitInfo.Count = 1;
    WaitInfo.MilliSeconds = INFINITY;
    WaitInfo.Objects[0] = ProcessInfo.Process;

    Result = Wait(&WaitInfo);

    if (Result == WAIT_TIMEOUT) {
        DEBUG(TEXT("[Spawn] Process wait timed out"));
        return MAX_U32;
    } else if (Result != WAIT_OBJECT_0) {
        DEBUG(TEXT("[Spawn] Process wait failed: %d"), Result);
        return MAX_U32;
    }

    DEBUG(TEXT("[Spawn] Process completed successfully, exit code: %d"), WaitInfo.ExitCodes[0]);
    return WaitInfo.ExitCodes[0];
}

/************************************************************************/

void SetProcessStatus(LPPROCESS This, U32 Status) {
    LockMutex(MUTEX_PROCESS, INFINITY);

    SAFE_USE_VALID_ID(This, KOID_PROCESS) {
        This->Status = Status;

        DEBUG(TEXT("[SetProcessStatus] Marked process %s as %d"), This->FileName, Status);

        if (Status == PROCESS_STATUS_DEAD) {
            // Store termination state in cache before process is destroyed
            StoreObjectTerminationState(This, This->ExitCode);
        }
    }

    UnlockMutex(MUTEX_PROCESS);
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

    SAFE_USE_VALID_ID(Process, KOID_PROCESS) {
        LockMutex(&(Process->Mutex), INFINITY);

        HeapBase = Process->HeapBase;

        UnlockMutex(&(Process->Mutex));
    }

    return HeapBase;
}

/***************************************************************************/

/**
 * @brief Output process information to the kernel log.
 *
 * @param Process Process to dump. Nothing is logged if NULL.
 */
void DumpProcess(LPPROCESS Process) {
    SAFE_USE_VALID_ID(Process, KOID_PROCESS) {
        LockMutex(&(Process->Mutex), INFINITY);

        DEBUG(TEXT("Address        : %p\n"), Process);
        DEBUG(TEXT("References     : %d\n"), Process->References);
        DEBUG(TEXT("OwnerProcess   : %p\n"), Process->OwnerProcess);
        DEBUG(TEXT("Privilege      : %d\n"), Process->Privilege);
        DEBUG(TEXT("Page directory : %p\n"), Process->PageDirectory);
        DEBUG(TEXT("File name      : %s\n"), Process->FileName);
        DEBUG(TEXT("Heap base      : %p\n"), Process->HeapBase);
        DEBUG(TEXT("Heap size      : %d\n"), Process->HeapSize);

        UnlockMutex(&(Process->Mutex));
    }
}

/***************************************************************************/

/**
 * @brief Initialize a SECURITY structure.
 *
 * @param This SECURITY structure to initialize.
 */
void InitSecurity(LPSECURITY This) {
    SAFE_USE(This) {
        This->TypeID = KOID_SECURITY;
        This->References = 1;
        This->OwnerProcess = GetCurrentProcess();
        This->Next = NULL;
        This->Prev = NULL;
        This->Owner = U64_Make(0, 0);
        This->UserPermissionCount = 0;
        This->DefaultPermissions = PERMISSION_NONE;
    }
}
