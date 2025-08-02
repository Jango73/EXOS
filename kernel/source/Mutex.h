
/***************************************************************************\

    EXOS Kernel
    Copyright (c) 1999-2025 Jango73
    All rights reserved

\***************************************************************************/

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
    { ID_MUTEX, 1, NULL, NULL, NULL, NULL, 0 }

#endif
