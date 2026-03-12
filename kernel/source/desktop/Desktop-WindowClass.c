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


    Desktop window class registry

\************************************************************************/

#include "Desktop-WindowClass.h"
#include "Desktop-Private.h"

#include "CoreString.h"
#include "Kernel.h"

/************************************************************************/

/**
 * @brief Find one class by name in one list without taking ownership.
 * @param List Class list.
 * @param Name Class name.
 * @return Class pointer or NULL.
 */
static LPWINDOW_CLASS WindowClassFindByNameUnlocked(LPLIST List, LPCSTR Name) {
    LPLISTNODE Node;
    LPWINDOW_CLASS This;

    if (List == NULL || Name == NULL) return NULL;

    for (Node = List->First; Node != NULL; Node = Node->Next) {
        This = (LPWINDOW_CLASS)Node;
        if (This == NULL || This->TypeID != KOID_WINDOW_CLASS) continue;
        if (StringCompare(This->Name, Name) == 0) return This;
    }

    return NULL;
}

/************************************************************************/

/**
 * @brief Find one class by identifier in one list without taking ownership.
 * @param List Class list.
 * @param ClassID Class identifier.
 * @return Class pointer or NULL.
 */
static LPWINDOW_CLASS WindowClassFindByHandleUnlocked(LPLIST List, U32 ClassID) {
    LPLISTNODE Node;
    LPWINDOW_CLASS This;

    if (List == NULL || ClassID == 0) return NULL;

    for (Node = List->First; Node != NULL; Node = Node->Next) {
        This = (LPWINDOW_CLASS)Node;
        if (This == NULL || This->TypeID != KOID_WINDOW_CLASS) continue;
        if (This->ClassID == ClassID) return This;
    }

    return NULL;
}

/************************************************************************/

/**
 * @brief Resolve one next class identifier from one list snapshot.
 * @param List Class list.
 * @return Next non-zero class identifier.
 */
static U32 WindowClassResolveNextIDUnlocked(LPLIST List) {
    LPLISTNODE Node;
    LPWINDOW_CLASS This;
    U32 MaximumClassID = 0;

    if (List == NULL) return 1;

    for (Node = List->First; Node != NULL; Node = Node->Next) {
        This = (LPWINDOW_CLASS)Node;
        if (This == NULL || This->TypeID != KOID_WINDOW_CLASS) continue;
        if (This->ClassID > MaximumClassID) MaximumClassID = This->ClassID;
    }

    return MaximumClassID + 1;
}

/************************************************************************/

/**
 * @brief Check whether one class has registered derived classes.
 * @param List Class list.
 * @param BaseClass Base class to resolve.
 * @return TRUE when at least one derived class exists.
 */
static BOOL WindowClassHasDerivedClassUnlocked(LPLIST List, LPWINDOW_CLASS BaseClass) {
    LPLISTNODE Node;
    LPWINDOW_CLASS This;

    if (List == NULL || BaseClass == NULL) return FALSE;

    for (Node = List->First; Node != NULL; Node = Node->Next) {
        This = (LPWINDOW_CLASS)Node;
        if (This == NULL || This->TypeID != KOID_WINDOW_CLASS) continue;
        if (This == BaseClass) continue;
        if (This->BaseClass == BaseClass) return TRUE;
    }

    return FALSE;
}

/************************************************************************/

/**
 * @brief Check whether one class is in use by one window tree.
 * @param Window Root window.
 * @param WindowClass Class to check.
 * @return TRUE when one window uses the class.
 */
static BOOL WindowClassIsUsedByWindowTree(LPWINDOW Window, LPWINDOW_CLASS WindowClass) {
    LPWINDOW* Children = NULL;
    BOOL IsUsed = FALSE;
    UINT ChildCount = 0;
    UINT ChildIndex;
    WINDOW_STATE_SNAPSHOT Snapshot;

    if (Window == NULL || Window->TypeID != KOID_WINDOW) return FALSE;
    if (WindowClass == NULL || WindowClass->TypeID != KOID_WINDOW_CLASS) return FALSE;

    if (GetWindowStateSnapshot(Window, &Snapshot) == FALSE) return FALSE;
    if (Snapshot.Class == WindowClass) {
        return TRUE;
    }

    (void)DesktopSnapshotWindowChildren(Window, &Children, &ChildCount);
    for (ChildIndex = 0; ChildIndex < ChildCount; ChildIndex++) {
        if (WindowClassIsUsedByWindowTree(Children[ChildIndex], WindowClass) != FALSE) {
            IsUsed = TRUE;
            break;
        }
    }
    if (Children != NULL) {
        KernelHeapFree(Children);
    }

    return IsUsed;
}

/************************************************************************/

/**
 * @brief Check whether one class is used by at least one active window.
 * @param WindowClass Class to check.
 * @return TRUE when at least one window uses the class.
 */
static BOOL WindowClassIsUsedByDesktopWindows(LPWINDOW_CLASS WindowClass) {
    LPLIST DesktopList;
    LPLISTNODE Node;
    LPDESKTOP Desktop;
    BOOL IsUsed = FALSE;

    if (WindowClass == NULL || WindowClass->TypeID != KOID_WINDOW_CLASS) return FALSE;

    DesktopList = GetDesktopList();
    if (DesktopList == NULL) return FALSE;

    for (Node = DesktopList->First; Node != NULL; Node = Node->Next) {
        Desktop = (LPDESKTOP)Node;
        if (Desktop == NULL || Desktop->TypeID != KOID_DESKTOP) continue;

        LockMutex(&(Desktop->Mutex), INFINITY);
        IsUsed = WindowClassIsUsedByWindowTree(Desktop->Window, WindowClass);
        UnlockMutex(&(Desktop->Mutex));

        if (IsUsed != FALSE) return TRUE;
    }

    return FALSE;
}

/************************************************************************/

/**
 * @brief Register one window class entry in the global registry.
 * @param Name Unique class name.
 * @param BaseClass Optional base class.
 * @param Function Class window procedure.
 * @param ClassDataSize Class-private allocation size.
 * @param OwnerProcess Owner process for user classes, NULL for kernel classes.
 * @return Registered class or NULL on failure.
 */
static LPWINDOW_CLASS WindowClassRegister(
    LPCSTR Name,
    LPWINDOW_CLASS BaseClass,
    WINDOWFUNC Function,
    U32 ClassDataSize,
    LPPROCESS OwnerProcess) {
    LPLIST ClassList;
    LPWINDOW_CLASS This;

    if (Name == NULL || Function == NULL) return NULL;

    ClassList = GetWindowClassList();
    if (ClassList == NULL) return NULL;

    LockMutex(MUTEX_KERNEL, INFINITY);

    if (WindowClassFindByNameUnlocked(ClassList, Name) != NULL) {
        UnlockMutex(MUTEX_KERNEL);
        return NULL;
    }

    if (BaseClass != NULL && WindowClassFindByHandleUnlocked(ClassList, BaseClass->ClassID) == NULL) {
        UnlockMutex(MUTEX_KERNEL);
        return NULL;
    }

    This = (LPWINDOW_CLASS)KernelHeapAlloc(sizeof(WINDOW_CLASS));
    if (This == NULL) {
        UnlockMutex(MUTEX_KERNEL);
        return NULL;
    }

    MemorySet(This, 0, sizeof(WINDOW_CLASS));
    This->TypeID = KOID_WINDOW_CLASS;
    This->References = 1;
    This->OwnerProcess = OwnerProcess;
    StringCopy(This->Name, Name);
    This->BaseClass = BaseClass;
    This->Function = Function;
    This->ClassDataSize = ClassDataSize;
    This->ClassID = WindowClassResolveNextIDUnlocked(ClassList);

    ListAddHead(ClassList, This);

    UnlockMutex(MUTEX_KERNEL);
    return This;
}

/************************************************************************/

/**
 * @brief Unregister one class from the global registry.
 * @param ClassID Optional class identifier.
 * @param Name Optional class name.
 * @param OwnerProcess Owner process for user classes, NULL for kernel classes.
 * @return TRUE on success.
 */
static BOOL WindowClassUnregister(U32 ClassID, LPCSTR Name, LPPROCESS OwnerProcess) {
    LPLIST ClassList;
    LPWINDOW_CLASS This;

    if (ClassID == 0 && Name == NULL) return FALSE;

    ClassList = GetWindowClassList();
    if (ClassList == NULL) return FALSE;

    LockMutex(MUTEX_KERNEL, INFINITY);

    if (ClassID != 0) {
        This = WindowClassFindByHandleUnlocked(ClassList, ClassID);
    } else {
        This = WindowClassFindByNameUnlocked(ClassList, Name);
    }

    if (This == NULL || This->TypeID != KOID_WINDOW_CLASS) {
        UnlockMutex(MUTEX_KERNEL);
        return FALSE;
    }

    if (This->OwnerProcess != OwnerProcess || StringCompare(This->Name, WINDOW_CLASS_DEFAULT_NAME) == 0 ||
        WindowClassHasDerivedClassUnlocked(ClassList, This) != FALSE) {
        UnlockMutex(MUTEX_KERNEL);
        return FALSE;
    }

    UnlockMutex(MUTEX_KERNEL);

    if (WindowClassIsUsedByDesktopWindows(This) != FALSE) return FALSE;

    LockMutex(MUTEX_KERNEL, INFINITY);

    if (ClassID != 0) {
        This = WindowClassFindByHandleUnlocked(ClassList, ClassID);
    } else {
        This = WindowClassFindByNameUnlocked(ClassList, Name);
    }

    if (This == NULL || This->TypeID != KOID_WINDOW_CLASS || This->OwnerProcess != OwnerProcess ||
        WindowClassHasDerivedClassUnlocked(ClassList, This) != FALSE) {
        UnlockMutex(MUTEX_KERNEL);
        return FALSE;
    }

    ListRemove(ClassList, This);
    UnlockMutex(MUTEX_KERNEL);

    KernelHeapFree(This);
    return TRUE;
}

/************************************************************************/

HANDLE RegisterWindowClass(LPCSTR ClassName, HANDLE BaseClass, LPCSTR BaseClassName, WINDOWFUNC Function, U32 ClassDataSize) {
    LPWINDOW_CLASS WindowClass = NULL;
    LPPROCESS Process = GetCurrentProcess();

    if (Process == &KernelProcess) {
        LPWINDOW_CLASS KernelBaseClass = NULL;

        if (WindowClassInitializeRegistry() == FALSE) return NULL;
        if (BaseClass != 0) {
            KernelBaseClass = WindowClassFindByHandle((U32)BaseClass);
        } else if (BaseClassName != NULL) {
            KernelBaseClass = WindowClassFindByName(BaseClassName);
        }

        WindowClass = WindowClassRegisterKernelClass(ClassName, KernelBaseClass, Function, ClassDataSize);
    } else {
        WindowClass = WindowClassRegisterUserClass(ClassName, (U32)BaseClass, BaseClassName, Function, ClassDataSize, Process);
    }

    if (WindowClass == NULL || WindowClass->TypeID != KOID_WINDOW_CLASS) return NULL;
    return (HANDLE)(UINT)WindowClass->ClassID;
}

/************************************************************************/

BOOL UnregisterWindowClass(HANDLE WindowClass, LPCSTR ClassName) {
    LPPROCESS Process = GetCurrentProcess();

    return WindowClassUnregister((U32)WindowClass, ClassName, Process == &KernelProcess ? NULL : Process);
}

/************************************************************************/

HANDLE FindWindowClass(LPCSTR ClassName) {
    LPWINDOW_CLASS WindowClass;

    WindowClass = WindowClassFindByName(ClassName);
    if (WindowClass == NULL || WindowClass->TypeID != KOID_WINDOW_CLASS) return NULL;

    return (HANDLE)(UINT)WindowClass->ClassID;
}

/************************************************************************/

BOOL WindowClassInitializeRegistry(void) {
    if (WindowClassGetDefault() != NULL) return TRUE;
    return WindowClassRegisterKernelClass(WINDOW_CLASS_DEFAULT_NAME, NULL, BaseWindowFunc, 0) != NULL;
}

/************************************************************************/

LPWINDOW_CLASS WindowClassRegisterKernelClass(LPCSTR Name, LPWINDOW_CLASS BaseClass, WINDOWFUNC Function, U32 ClassDataSize) {
    return WindowClassRegister(Name, BaseClass, Function, ClassDataSize, NULL);
}

/************************************************************************/

LPWINDOW_CLASS WindowClassRegisterUserClass(
    LPCSTR Name,
    U32 BaseClassID,
    LPCSTR BaseClassName,
    WINDOWFUNC Function,
    U32 ClassDataSize,
    LPPROCESS OwnerProcess) {
    LPWINDOW_CLASS BaseClass = NULL;

    if (OwnerProcess == NULL || OwnerProcess->TypeID != KOID_PROCESS) return NULL;
    if (Name == NULL || Function == NULL) return NULL;

    if (BaseClassID != 0) {
        BaseClass = WindowClassFindByHandle(BaseClassID);
    } else if (BaseClassName != NULL) {
        BaseClass = WindowClassFindByName(BaseClassName);
    }

    return WindowClassRegister(Name, BaseClass, Function, ClassDataSize, OwnerProcess);
}

/************************************************************************/

BOOL WindowClassUnregisterUserClass(U32 ClassID, LPCSTR Name, LPPROCESS OwnerProcess) {
    if (OwnerProcess == NULL || OwnerProcess->TypeID != KOID_PROCESS) return FALSE;
    return WindowClassUnregister(ClassID, Name, OwnerProcess);
}

/************************************************************************/

LPWINDOW_CLASS WindowClassFindByName(LPCSTR Name) {
    LPLIST ClassList;
    LPWINDOW_CLASS This;

    if (Name == NULL) return NULL;

    ClassList = GetWindowClassList();
    if (ClassList == NULL) return NULL;

    LockMutex(MUTEX_KERNEL, INFINITY);
    This = WindowClassFindByNameUnlocked(ClassList, Name);
    UnlockMutex(MUTEX_KERNEL);

    return This;
}

/************************************************************************/

LPWINDOW_CLASS WindowClassFindByHandle(U32 ClassID) {
    LPLIST ClassList;
    LPWINDOW_CLASS This;

    if (ClassID == 0) return NULL;

    ClassList = GetWindowClassList();
    if (ClassList == NULL) return NULL;

    LockMutex(MUTEX_KERNEL, INFINITY);
    This = WindowClassFindByHandleUnlocked(ClassList, ClassID);
    UnlockMutex(MUTEX_KERNEL);

    return This;
}

/************************************************************************/

LPWINDOW_CLASS WindowClassGetDefault(void) {
    return WindowClassFindByName(WINDOW_CLASS_DEFAULT_NAME);
}

/************************************************************************/
