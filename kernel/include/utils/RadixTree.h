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


    Radix Tree

\************************************************************************/

#ifndef RADIX_TREE_H_INCLUDED
#define RADIX_TREE_H_INCLUDED

/************************************************************************/

#include "Base.h"

/************************************************************************/

typedef struct tag_RADIX_TREE RADIX_TREE, *LPRADIX_TREE;

typedef BOOL (*RADIX_TREE_VISITOR)(UINT Handle, LINEAR Value, LPVOID Context);

/************************************************************************/

LPRADIX_TREE RadixTreeCreate(void);
void RadixTreeDestroy(LPRADIX_TREE Tree);
BOOL RadixTreeInsert(LPRADIX_TREE Tree, UINT Handle, LINEAR Value);
BOOL RadixTreeRemove(LPRADIX_TREE Tree, UINT Handle);
LINEAR RadixTreeFind(LPRADIX_TREE Tree, UINT Handle);
BOOL RadixTreeIterate(LPRADIX_TREE Tree, RADIX_TREE_VISITOR Visitor, LPVOID Context);
UINT RadixTreeGetCount(const RADIX_TREE* Tree);

/************************************************************************/

#endif  // RADIX_TREE_H_INCLUDED

