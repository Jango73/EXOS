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


    Handle Map

\************************************************************************/

#ifndef HANDLE_MAP_H_INCLUDED
#define HANDLE_MAP_H_INCLUDED

/************************************************************************/

#include "Base.h"
#include "Mutex.h"
#include "utils/BlockList.h"
#include "utils/RadixTree.h"

/************************************************************************/

#define HANDLE_MAP_OK ((UINT)0x00000000)
#define HANDLE_MAP_ERROR_INVALID_PARAMETER ((UINT)0x00000001)
#define HANDLE_MAP_ERROR_OUT_OF_HANDLES ((UINT)0x00000002)
#define HANDLE_MAP_ERROR_NOT_FOUND ((UINT)0x00000003)
#define HANDLE_MAP_ERROR_ALREADY_ATTACHED ((UINT)0x00000004)
#define HANDLE_MAP_ERROR_NOT_ATTACHED ((UINT)0x00000005)
#define HANDLE_MAP_ERROR_OUT_OF_MEMORY ((UINT)0x00000006)
#define HANDLE_MAP_ERROR_INTERNAL ((UINT)0x00000007)

/************************************************************************/

typedef struct tag_HANDLE_MAP {
    LPRADIX_TREE Tree;
    BLOCK_LIST EntryAllocator;
    MUTEX Mutex;
    UINT NextHandle;
} HANDLE_MAP, *LPHANDLE_MAP;

/************************************************************************/

void HandleMapInit(LPHANDLE_MAP Map);
UINT HandleMapAllocateHandle(LPHANDLE_MAP Map, UINT* HandleOut);
UINT HandleMapReleaseHandle(LPHANDLE_MAP Map, UINT Handle);
UINT HandleMapResolveHandle(LPHANDLE_MAP Map, UINT Handle, LINEAR* PointerOut);
UINT HandleMapAttachPointer(LPHANDLE_MAP Map, UINT Handle, LINEAR Pointer);
UINT HandleMapDetachPointer(LPHANDLE_MAP Map, UINT Handle, LINEAR* PointerOut);
UINT HandleMapFindHandleByPointer(LPHANDLE_MAP Map, LINEAR Pointer, UINT* HandleOut);

/************************************************************************/

#endif  // HANDLE_MAP_H_INCLUDED
