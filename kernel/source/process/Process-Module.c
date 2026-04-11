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


    Process executable module bindings

\************************************************************************/

#include "process/Process-Module.h"

#include "core/Kernel.h"
#include "log/Log.h"
#include "text/CoreString.h"

/***************************************************************************/

/**
 * @brief Find one module binding in one process without taking locks.
 *
 * @param Process Target process.
 * @param Image Shared module image to match.
 * @return Matching binding or NULL.
 */
static LPEXECUTABLE_MODULE_BINDING FindProcessModuleBindingLocked(
    LPPROCESS Process,
    LPEXECUTABLE_MODULE_IMAGE Image) {
    LPLIST BindingList = NULL;

    if (Process == NULL || Image == NULL) {
        return NULL;
    }

    BindingList = Process->ModuleBindings;
    if (BindingList == NULL) {
        return NULL;
    }

    for (LPLISTNODE Node = BindingList->First; Node != NULL; Node = Node->Next) {
        LPEXECUTABLE_MODULE_BINDING Binding = (LPEXECUTABLE_MODULE_BINDING)Node;

        if (Binding == NULL) continue;
        if (Binding->Image != Image) continue;

        return Binding;
    }

    return NULL;
}

/***************************************************************************/

/**
 * @brief Create one empty dependency edge entry.
 *
 * @param Dependency Referenced dependency binding.
 * @return New edge node or NULL on allocation failure.
 */
static LPEXECUTABLE_MODULE_BINDING_DEPENDENCY CreateExecutableModuleBindingDependency(
    LPEXECUTABLE_MODULE_BINDING Dependency) {
    LPEXECUTABLE_MODULE_BINDING_DEPENDENCY Edge = NULL;

    Edge = (LPEXECUTABLE_MODULE_BINDING_DEPENDENCY)KernelHeapAlloc(sizeof(EXECUTABLE_MODULE_BINDING_DEPENDENCY));
    if (Edge == NULL) {
        return NULL;
    }

    MemorySet(Edge, 0, sizeof(EXECUTABLE_MODULE_BINDING_DEPENDENCY));
    Edge->Binding = Dependency;
    return Edge;
}

/***************************************************************************/

/**
 * @brief Decrement one process binding reference while the owner is locked.
 *
 * @param Binding Binding whose process reference must be dropped.
 */
static void ReleaseProcessModuleBindingLocked(LPEXECUTABLE_MODULE_BINDING Binding) {
    LPPROCESS Process = NULL;

    if (Binding == NULL) {
        return;
    }

    if (Binding->ProcessReferences > 0) {
        Binding->ProcessReferences--;
    }

    if (Binding->ProcessReferences > 0) {
        return;
    }

    Process = Binding->Process;
    if (Process != NULL && Process->ModuleBindings != NULL) {
        ListRemove(Process->ModuleBindings, Binding);

        if (Process->ModuleBindingCount > 0) {
            Process->ModuleBindingCount--;
        }
    }

    Binding->Process = NULL;
    Binding->OwnerProcess = NULL;
    DestroyKernelObject(Binding);
}

/***************************************************************************/

/**
 * @brief Free all dependency edges owned by one process module binding.
 *
 * @param Binding Binding whose dependency list must be destroyed.
 */
static void DeleteExecutableModuleBindingDependencies(LPEXECUTABLE_MODULE_BINDING Binding) {
    if (Binding == NULL || Binding->Dependencies == NULL) {
        return;
    }

    while (Binding->Dependencies->First != NULL) {
        LPEXECUTABLE_MODULE_BINDING_DEPENDENCY Edge =
            (LPEXECUTABLE_MODULE_BINDING_DEPENDENCY)Binding->Dependencies->First;

        ListRemove(Binding->Dependencies, Edge);

        if (Edge->Binding != NULL) {
            ReleaseProcessModuleBindingLocked(Edge->Binding);
        }

        KernelHeapFree(Edge);
    }

    DeleteList(Binding->Dependencies);
    Binding->Dependencies = NULL;
}

/***************************************************************************/

/**
 * @brief Allocate one new binding object for one process and one module image.
 *
 * @param Process Owning process.
 * @param Image Shared module image.
 * @return New binding or NULL.
 */
static LPEXECUTABLE_MODULE_BINDING CreateProcessModuleBinding(
    LPPROCESS Process,
    LPEXECUTABLE_MODULE_IMAGE Image) {
    LPEXECUTABLE_MODULE_BINDING Binding = NULL;

    Binding = (LPEXECUTABLE_MODULE_BINDING)CreateKernelObject(
        sizeof(EXECUTABLE_MODULE_BINDING),
        KOID_EXECUTABLE_MODULE_BINDING);
    if (Binding == NULL) {
        return NULL;
    }

    SetKernelObjectDestructor(Binding, (OBJECTDESTRUCTOR)DeleteExecutableModuleBinding);
    Binding->Process = Process;
    Binding->OwnerProcess = Process;
    Binding->Image = Image;
    Binding->ProcessReferences = 1;
    Binding->StateFlags = EXECUTABLE_MODULE_BINDING_STATE_CREATED;
    InitMutex(&(Binding->Mutex));
    SetMutexDebugInfo(&(Binding->Mutex), MUTEX_CLASS_KERNEL, TEXT("ExecutableModuleBinding"));

    Binding->Dependencies = NewList(NULL, KernelHeapAlloc, KernelHeapFree);
    if (Binding->Dependencies == NULL) {
        DestroyKernelObject(Binding);
        return NULL;
    }

    RetainExecutableModuleImage(Image);
    return Binding;
}

/***************************************************************************/

/**
 * @brief Ensure one process owns a binding list.
 *
 * @param Process Target process.
 * @return TRUE when the binding list exists.
 */
BOOL InitializeProcessModuleBindings(LPPROCESS Process) {
    SAFE_USE_VALID_ID(Process, KOID_PROCESS) {
        if (Process->ModuleBindings != NULL) {
            return TRUE;
        }

        Process->ModuleBindings = NewList(NULL, KernelHeapAlloc, KernelHeapFree);
        Process->ModuleBindingCount = 0;
        return Process->ModuleBindings != NULL;
    }

    return FALSE;
}

/***************************************************************************/

/**
 * @brief Destroy all executable module bindings owned by one process.
 *
 * @param Process Target process.
 */
void DeleteProcessModuleBindings(LPPROCESS Process) {
    SAFE_USE_VALID_ID(Process, KOID_PROCESS) {
        LockMutex(&(Process->Mutex), INFINITY);

        if (Process->ModuleBindings != NULL) {
            while (Process->ModuleBindings->First != NULL) {
                LPEXECUTABLE_MODULE_BINDING Binding =
                    (LPEXECUTABLE_MODULE_BINDING)Process->ModuleBindings->First;

                ListRemove(Process->ModuleBindings, Binding);
                if (Process->ModuleBindingCount > 0) {
                    Process->ModuleBindingCount--;
                }

                Binding->Process = NULL;
                Binding->OwnerProcess = NULL;
                DestroyKernelObject(Binding);
            }

            DeleteList(Process->ModuleBindings);
            Process->ModuleBindings = NULL;
        }

        Process->ModuleBindingCount = 0;
        UnlockMutex(&(Process->Mutex));
    }
}

/***************************************************************************/

/**
 * @brief Return the number of executable module bindings attached to one process.
 *
 * @param Process Target process.
 * @return Binding count.
 */
UINT GetProcessModuleBindingCount(LPPROCESS Process) {
    UINT Count = 0;

    SAFE_USE_VALID_ID(Process, KOID_PROCESS) {
        LockMutex(&(Process->Mutex), INFINITY);
        Count = Process->ModuleBindingCount;
        UnlockMutex(&(Process->Mutex));
    }

    return Count;
}

/***************************************************************************/

/**
 * @brief Find one executable module binding owned by one process.
 *
 * @param Process Target process.
 * @param Image Shared module image to match.
 * @return Matching binding or NULL.
 */
LPEXECUTABLE_MODULE_BINDING FindProcessModuleBinding(LPPROCESS Process, LPEXECUTABLE_MODULE_IMAGE Image) {
    LPEXECUTABLE_MODULE_BINDING Binding = NULL;

    SAFE_USE_VALID_ID(Process, KOID_PROCESS) {
        SAFE_USE_VALID_ID(Image, KOID_EXECUTABLE_MODULE_IMAGE) {
            LockMutex(&(Process->Mutex), INFINITY);
            Binding = FindProcessModuleBindingLocked(Process, Image);
            UnlockMutex(&(Process->Mutex));
        }
    }

    return Binding;
}

/***************************************************************************/

/**
 * @brief Acquire one process-owned binding for one shared module image.
 *
 * Reuses an existing binding when the process already loaded the module.
 *
 * @param Process Target process.
 * @param Image Shared module image.
 * @return Existing or newly created binding.
 */
LPEXECUTABLE_MODULE_BINDING AcquireProcessModuleBinding(LPPROCESS Process, LPEXECUTABLE_MODULE_IMAGE Image) {
    LPEXECUTABLE_MODULE_BINDING Binding = NULL;

    SAFE_USE_VALID_ID(Process, KOID_PROCESS) {
        SAFE_USE_VALID_ID(Image, KOID_EXECUTABLE_MODULE_IMAGE) {
            LockMutex(&(Process->Mutex), INFINITY);

            if (!InitializeProcessModuleBindings(Process)) {
                UnlockMutex(&(Process->Mutex));
                return NULL;
            }

            Binding = FindProcessModuleBindingLocked(Process, Image);
            if (Binding != NULL) {
                Binding->ProcessReferences++;
                UnlockMutex(&(Process->Mutex));
                return Binding;
            }

            Binding = CreateProcessModuleBinding(Process, Image);
            if (Binding == NULL) {
                UnlockMutex(&(Process->Mutex));
                return NULL;
            }

            if (!ListAddItem(Process->ModuleBindings, Binding)) {
                DestroyKernelObject(Binding);
                UnlockMutex(&(Process->Mutex));
                return NULL;
            }

            Process->ModuleBindingCount++;
            UnlockMutex(&(Process->Mutex));
        }
    }

    return Binding;
}

/***************************************************************************/

/**
 * @brief Release one process-owned binding reference.
 *
 * @param Binding Binding to release.
 */
void ReleaseProcessModuleBinding(LPEXECUTABLE_MODULE_BINDING Binding) {
    LPPROCESS Process = NULL;

    SAFE_USE_VALID_ID(Binding, KOID_EXECUTABLE_MODULE_BINDING) {
        Process = Binding->Process;
        SAFE_USE_VALID_ID(Process, KOID_PROCESS) {
            LockMutex(&(Process->Mutex), INFINITY);
            ReleaseProcessModuleBindingLocked(Binding);
            UnlockMutex(&(Process->Mutex));
        }
    }
}

/***************************************************************************/

/**
 * @brief Store one per-segment base address on one process binding.
 *
 * @param Process Owning process.
 * @param Binding Target binding.
 * @param SegmentIndex Segment index in executable metadata.
 * @param Base Installed base inside process address space.
 * @return TRUE on success.
 */
BOOL SetProcessModuleBindingSegmentBase(
    LPPROCESS Process,
    LPEXECUTABLE_MODULE_BINDING Binding,
    UINT SegmentIndex,
    LINEAR Base) {
    BOOL Result = FALSE;

    SAFE_USE_VALID_ID(Process, KOID_PROCESS) {
        SAFE_USE_VALID_ID(Binding, KOID_EXECUTABLE_MODULE_BINDING) {
            if (SegmentIndex >= EXECUTABLE_MAX_SEGMENTS) {
                return FALSE;
            }

            LockMutex(&(Process->Mutex), INFINITY);

            if (Binding->Process == Process) {
                Binding->SegmentBases[SegmentIndex] = Base;
                Binding->StateFlags |= EXECUTABLE_MODULE_BINDING_STATE_SEGMENTS_ASSIGNED;
                Result = TRUE;
            }

            UnlockMutex(&(Process->Mutex));
        }
    }

    return Result;
}

/***************************************************************************/

/**
 * @brief Store process-private layout addresses on one binding.
 *
 * @param Process Owning process.
 * @param Binding Target binding.
 * @param WritableDataBase Writable module data base.
 * @param GlobalOffsetTableBase Process-global GOT base if used.
 * @param ProcedureLinkageTableBase Process-global PLT base if used.
 * @param BookkeepingBase Module bookkeeping base if used.
 * @param StateFlags State bits to OR into the binding state.
 * @return TRUE on success.
 */
BOOL SetProcessModuleBindingLayout(
    LPPROCESS Process,
    LPEXECUTABLE_MODULE_BINDING Binding,
    LINEAR WritableDataBase,
    LINEAR GlobalOffsetTableBase,
    LINEAR ProcedureLinkageTableBase,
    LINEAR BookkeepingBase,
    U32 StateFlags) {
    BOOL Result = FALSE;

    SAFE_USE_VALID_ID(Process, KOID_PROCESS) {
        SAFE_USE_VALID_ID(Binding, KOID_EXECUTABLE_MODULE_BINDING) {
            LockMutex(&(Process->Mutex), INFINITY);

            if (Binding->Process == Process) {
                Binding->WritableDataBase = WritableDataBase;
                Binding->GlobalOffsetTableBase = GlobalOffsetTableBase;
                Binding->ProcedureLinkageTableBase = ProcedureLinkageTableBase;
                Binding->BookkeepingBase = BookkeepingBase;
                Binding->StateFlags |= StateFlags;
                Result = TRUE;
            }

            UnlockMutex(&(Process->Mutex));
        }
    }

    return Result;
}

/***************************************************************************/

/**
 * @brief Record one dependency edge between two bindings of the same process.
 *
 * @param Process Owning process.
 * @param Binding Binding that depends on another binding.
 * @param Dependency Binding required by @p Binding.
 * @return TRUE when the dependency edge exists.
 */
BOOL AddProcessModuleBindingDependency(
    LPPROCESS Process,
    LPEXECUTABLE_MODULE_BINDING Binding,
    LPEXECUTABLE_MODULE_BINDING Dependency) {
    LPEXECUTABLE_MODULE_BINDING_DEPENDENCY Edge = NULL;
    BOOL Result = FALSE;

    SAFE_USE_VALID_ID(Process, KOID_PROCESS) {
        SAFE_USE_VALID_ID(Binding, KOID_EXECUTABLE_MODULE_BINDING) {
            SAFE_USE_VALID_ID(Dependency, KOID_EXECUTABLE_MODULE_BINDING) {
                LockMutex(&(Process->Mutex), INFINITY);

                if (Binding->Process == Process && Dependency->Process == Process && Binding != Dependency &&
                    Binding->Dependencies != NULL) {
                    for (LPLISTNODE Node = Binding->Dependencies->First; Node != NULL; Node = Node->Next) {
                        LPEXECUTABLE_MODULE_BINDING_DEPENDENCY Existing =
                            (LPEXECUTABLE_MODULE_BINDING_DEPENDENCY)Node;

                        if (Existing->Binding == Dependency) {
                            Binding->StateFlags |= EXECUTABLE_MODULE_BINDING_STATE_DEPENDENCIES_RESOLVED;
                            UnlockMutex(&(Process->Mutex));
                            return TRUE;
                        }
                    }

                    Edge = CreateExecutableModuleBindingDependency(Dependency);
                    if (Edge != NULL && ListAddItem(Binding->Dependencies, Edge)) {
                        Dependency->ProcessReferences++;
                        Binding->StateFlags |= EXECUTABLE_MODULE_BINDING_STATE_DEPENDENCIES_RESOLVED;
                        Result = TRUE;
                    } else if (Edge != NULL) {
                        KernelHeapFree(Edge);
                    }
                }

                UnlockMutex(&(Process->Mutex));
            }
        }
    }

    return Result;
}

/***************************************************************************/

/**
 * @brief Destroy one process-owned executable module binding.
 *
 * @param Binding Binding to destroy.
 */
void DeleteExecutableModuleBinding(LPEXECUTABLE_MODULE_BINDING Binding) {
    SAFE_USE_VALID_ID(Binding, KOID_EXECUTABLE_MODULE_BINDING) {
        DeleteExecutableModuleBindingDependencies(Binding);

        if (Binding->Image != NULL) {
            ReleaseExecutableModuleImage(Binding->Image);
            Binding->Image = NULL;
        }

        Binding->TypeID = KOID_NONE;
        KernelHeapFree(Binding);
    }
}
