
/************************************************************************\

    EXOS Kernel
    Copyright (c) 1999-2026 Jango73

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


    Task executable module TLS

\************************************************************************/

#include "process/Task.h"

#include "core/Kernel.h"
#include "log/Log.h"
#include "memory/Heap.h"
#include "memory/Memory.h"
#include "process/Process-Arena.h"
#include "process/Process-Module.h"
#include "process/Schedule.h"

/************************************************************************/

/**
 * @brief Destroy one task-owned module TLS block.
 *
 * @param Block TLS block to destroy.
 */
static void DeleteTaskModuleTlsBlock(LPTASK_MODULE_TLS_BLOCK Block) {
    if (Block == NULL) {
        return;
    }

    if (Block->Base != 0 && Block->Size != 0 && Block->Binding != NULL && Block->Binding->Process != NULL) {
        FreeRegionForProcess(Block->Binding->Process, Block->Base, Block->Size);
    }

    KernelHeapFree(Block);
}

/************************************************************************/

/**
 * @brief Return the TLS block list owned by one task.
 *
 * @param Task Target task.
 * @return TLS block list or NULL.
 */
static LPLIST TaskGetModuleTlsBlockList(LPTASK Task) {
    if (Task == NULL) {
        return NULL;
    }

    if (Task->ModuleTlsBlocks != NULL) {
        return Task->ModuleTlsBlocks;
    }

    Task->ModuleTlsBlocks = NewList((LISTITEMDESTRUCTOR)DeleteTaskModuleTlsBlock, KernelHeapAlloc, KernelHeapFree);
    return Task->ModuleTlsBlocks;
}

/************************************************************************/

/**
 * @brief Find one TLS block for one module binding.
 *
 * @param Task Target task.
 * @param Binding Module binding to match.
 * @return Matching TLS block or NULL.
 */
static LPTASK_MODULE_TLS_BLOCK TaskFindModuleTlsBlock(
    LPTASK Task,
    LPEXECUTABLE_MODULE_BINDING Binding) {
    if (Task == NULL || Binding == NULL || Task->ModuleTlsBlocks == NULL) {
        return NULL;
    }

    for (LPLISTNODE Node = Task->ModuleTlsBlocks->First; Node != NULL; Node = Node->Next) {
        LPTASK_MODULE_TLS_BLOCK Block = (LPTASK_MODULE_TLS_BLOCK)Node;

        if (Block->Binding == Binding) {
            return Block;
        }
    }

    return NULL;
}

/************************************************************************/

/**
 * @brief Create one task-owned module TLS block.
 *
 * @param Binding Module binding that owns the TLS template.
 * @param TemplateBase Mapped template base inside the process.
 * @param TemplateSize Initialized template size.
 * @param TotalSize Total TLS block size.
 * @param Alignment Required TLS alignment.
 * @return New TLS block or NULL.
 */
static LPTASK_MODULE_TLS_BLOCK TaskCreateModuleTlsBlock(
    LPEXECUTABLE_MODULE_BINDING Binding,
    LINEAR TemplateBase,
    UINT TemplateSize,
    UINT TotalSize,
    UINT Alignment) {
    LPTASK_MODULE_TLS_BLOCK Block = NULL;
    LINEAR TlsBase;

    if (Binding == NULL || Binding->Process == NULL || TotalSize == 0 || TemplateSize > TotalSize) {
        return NULL;
    }

    if (TemplateSize != 0 && TemplateBase == 0) {
        return NULL;
    }

    TlsBase = ProcessArenaAllocateModule(Binding->Process,
                                         PROCESS_MODULE_ALLOCATION_TLS,
                                         TotalSize,
                                         ALLOC_PAGES_COMMIT | ALLOC_PAGES_READWRITE,
                                         TEXT("TaskModuleTls"));
    if (TlsBase == 0) {
        ERROR(TEXT("[TaskCreateModuleTlsBlock] Module TLS allocation failed task process=%p size=%u"),
              Binding->Process,
              TotalSize);
        return NULL;
    }

    MemorySet((LPVOID)TlsBase, 0, TotalSize);
    if (TemplateSize != 0) {
        MemoryCopy((LPVOID)TlsBase, (LPCVOID)TemplateBase, TemplateSize);
    }

    Block = (LPTASK_MODULE_TLS_BLOCK)KernelHeapAlloc(sizeof(TASK_MODULE_TLS_BLOCK));
    if (Block == NULL) {
        FreeRegionForProcess(Binding->Process, TlsBase, TotalSize);
        return NULL;
    }

    MemorySet(Block, 0, sizeof(TASK_MODULE_TLS_BLOCK));
    Block->Binding = Binding;
    Block->Base = TlsBase;
    Block->Size = TotalSize;
    Block->TemplateSize = TemplateSize;
    Block->Alignment = Alignment;
    return Block;
}

/************************************************************************/

/**
 * @brief Ensure one task owns a TLS block for one module binding.
 *
 * @param Task Target task.
 * @param Binding Module binding requiring TLS.
 * @param TemplateBase Mapped template base inside the process.
 * @param TemplateSize Initialized template size.
 * @param TotalSize Total TLS block size.
 * @param Alignment Required TLS alignment.
 * @return TRUE when the TLS block exists.
 */
BOOL TaskEnsureModuleTlsBlock(
    LPTASK Task,
    LPEXECUTABLE_MODULE_BINDING Binding,
    LINEAR TemplateBase,
    UINT TemplateSize,
    UINT TotalSize,
    UINT Alignment) {
    LPTASK_MODULE_TLS_BLOCK Block = NULL;
    LPLIST BlockList = NULL;
    BOOL Result = FALSE;

    SAFE_USE_VALID_ID(Task, KOID_TASK) {
        SAFE_USE_VALID_ID(Binding, KOID_EXECUTABLE_MODULE_BINDING) {
            LockMutex(&(Task->Mutex), INFINITY);

            if (Task->OwnerProcess != Binding->Process) {
                UnlockMutex(&(Task->Mutex));
                return FALSE;
            }

            if (TaskFindModuleTlsBlock(Task, Binding) != NULL) {
                UnlockMutex(&(Task->Mutex));
                return TRUE;
            }

            BlockList = TaskGetModuleTlsBlockList(Task);
            if (BlockList == NULL) {
                UnlockMutex(&(Task->Mutex));
                return FALSE;
            }

            Block = TaskCreateModuleTlsBlock(Binding, TemplateBase, TemplateSize, TotalSize, Alignment);
            if (Block != NULL) {
                FreezeScheduler();
                Result = ListAddItem(BlockList, Block);
                if (Result != FALSE) {
                    Task->ModuleTlsBlockCount++;
                }
                UnfreezeScheduler();

                if (Result == FALSE) {
                    DeleteTaskModuleTlsBlock(Block);
                }
            }

            UnlockMutex(&(Task->Mutex));
        }
    }

    return Result;
}

/************************************************************************/

/**
 * @brief Release one module TLS block owned by one task.
 *
 * @param Task Target task.
 * @param Binding Module binding whose TLS block must be released.
 */
void TaskReleaseModuleTlsBlock(LPTASK Task, LPEXECUTABLE_MODULE_BINDING Binding) {
    SAFE_USE_VALID_ID(Task, KOID_TASK) {
        SAFE_USE_VALID_ID(Binding, KOID_EXECUTABLE_MODULE_BINDING) {
            LockMutex(&(Task->Mutex), INFINITY);

            LPTASK_MODULE_TLS_BLOCK Block = TaskFindModuleTlsBlock(Task, Binding);
            if (Block != NULL) {
                FreezeScheduler();
                ListRemove(Task->ModuleTlsBlocks, Block);
                if (Task->ModuleTlsBlockCount > 0) {
                    Task->ModuleTlsBlockCount--;
                }
                UnfreezeScheduler();
                DeleteTaskModuleTlsBlock(Block);
            }

            UnlockMutex(&(Task->Mutex));
        }
    }
}

/************************************************************************/

/**
 * @brief Release all module TLS blocks owned by one task.
 *
 * @param Task Target task.
 */
void TaskReleaseModuleTlsBlocks(LPTASK Task) {
    SAFE_USE_VALID_ID(Task, KOID_TASK) {
        LockMutex(&(Task->Mutex), INFINITY);

        if (Task->ModuleTlsBlocks != NULL) {
            FreezeScheduler();
            DeleteList(Task->ModuleTlsBlocks);
            Task->ModuleTlsBlocks = NULL;
            Task->ModuleTlsBlockCount = 0;
            UnfreezeScheduler();
        }

        UnlockMutex(&(Task->Mutex));
    }
}

/************************************************************************/

/**
 * @brief Release one module TLS block from every task owned by a process.
 *
 * @param Process Target process.
 * @param Binding Module binding whose TLS blocks must be released.
 */
void TaskReleaseProcessModuleTlsBlocks(LPPROCESS Process, LPEXECUTABLE_MODULE_BINDING Binding) {
    LPLIST TaskList = GetTaskList();

    if (Process == NULL || Binding == NULL || TaskList == NULL) {
        return;
    }

    for (LPLISTNODE Node = TaskList->First; Node != NULL; Node = Node->Next) {
        LPTASK Task = (LPTASK)Node;

        if (Task == NULL || Task->OwnerProcess != Process) {
            continue;
        }

        TaskReleaseModuleTlsBlock(Task, Binding);
    }
}

/************************************************************************/

/**
 * @brief Install one module TLS block in every task owned by a process.
 *
 * @param Process Target process.
 * @param Binding Module binding requiring TLS.
 * @param TemplateBase Mapped template base inside the process.
 * @param TemplateSize Initialized template size.
 * @param TotalSize Total TLS block size.
 * @param Alignment Required TLS alignment.
 * @return TRUE when every task owns the TLS block.
 */
BOOL TaskInstallProcessModuleTlsBlocks(
    LPPROCESS Process,
    LPEXECUTABLE_MODULE_BINDING Binding,
    LINEAR TemplateBase,
    UINT TemplateSize,
    UINT TotalSize,
    UINT Alignment) {
    LPLIST TaskList = GetTaskList();

    if (Process == NULL || Binding == NULL || TaskList == NULL) {
        return FALSE;
    }

    for (LPLISTNODE Node = TaskList->First; Node != NULL; Node = Node->Next) {
        LPTASK Task = (LPTASK)Node;

        if (Task == NULL || Task->OwnerProcess != Process) {
            continue;
        }

        if (!TaskEnsureModuleTlsBlock(Task, Binding, TemplateBase, TemplateSize, TotalSize, Alignment)) {
            for (LPLISTNODE RollbackNode = TaskList->First; RollbackNode != Node; RollbackNode = RollbackNode->Next) {
                LPTASK RollbackTask = (LPTASK)RollbackNode;

                if (RollbackTask == NULL || RollbackTask->OwnerProcess != Process) {
                    continue;
                }

                TaskReleaseModuleTlsBlock(RollbackTask, Binding);
            }

            return FALSE;
        }
    }

    return TRUE;
}
