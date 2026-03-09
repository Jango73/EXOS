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
        if (This == NULL) continue;
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
        if (This == NULL) continue;
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
        if (This == NULL) continue;
        if (This->ClassID > MaximumClassID) MaximumClassID = This->ClassID;
    }

    return MaximumClassID + 1;
}

/************************************************************************/

BOOL WindowClassInitializeRegistry(void) {
    if (WindowClassGetDefault() != NULL) return TRUE;
    return WindowClassRegisterKernelClass(WINDOW_CLASS_DEFAULT_NAME, NULL, DefWindowFunc, 0) != NULL;
}

/************************************************************************/

LPWINDOW_CLASS WindowClassRegisterKernelClass(LPCSTR Name, LPWINDOW_CLASS BaseClass, WINDOWFUNC Function, U32 ClassDataSize) {
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
