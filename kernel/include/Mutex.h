
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


    Mutex

\************************************************************************/
#ifndef MUTEX_H_INCLUDED
#define MUTEX_H_INCLUDED

/***************************************************************************/

typedef struct tag_TASK TASK, *LPTASK;
typedef struct tag_PROCESS PROCESS, *LPPROCESS;

/***************************************************************************/
// The mutex structure

struct tag_MUTEX {
    LISTNODE_FIELDS         // Standard EXOS object fields
        LPPROCESS Process;  // Process that has locked this sem.
    LPTASK Task;            // Task that has locked this sem.
    U32 Lock;               // Lock count of this sem.
};

typedef struct tag_MUTEX MUTEX, *LPMUTEX;

// Macro to initialize a mutex

#define EMPTY_MUTEX \
    { .ID = ID_MUTEX, .References = 1, .Next = NULL, .Prev = NULL, .Process = NULL, .Task = NULL, .Lock = 0 }

#endif
