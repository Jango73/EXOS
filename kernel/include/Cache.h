
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


    Generic Temporary Cache with TTL

\************************************************************************/

#ifndef CACHE_H_INCLUDED
#define CACHE_H_INCLUDED

/************************************************************************/

#include "Base.h"
#include "List.h"
#include "Mutex.h"

/************************************************************************/

#define CACHE_DEFAULT_CAPACITY 256

/************************************************************************/

typedef struct {
    LPVOID Data;
    U32 ExpirationTime;
    BOOL Valid;
} TEMPORARY_CACHE_ENTRY, *LPTEMPORARY_CACHE_ENTRY;

typedef struct {
    LPTEMPORARY_CACHE_ENTRY Entries;
    U32 Capacity;
    U32 Count;
    MUTEX Mutex;
} TEMPORARY_CACHE, *LPTEMPORARY_CACHE;

/************************************************************************/

void CacheInit(LPTEMPORARY_CACHE Cache, U32 Capacity);
void CacheDeinit(LPTEMPORARY_CACHE Cache);
BOOL CacheAdd(LPTEMPORARY_CACHE Cache, LPVOID Data, U32 TTL_MS);
LPVOID CacheFind(LPTEMPORARY_CACHE Cache, BOOL (*Matcher)(LPVOID Data, LPVOID Context), LPVOID Context);
void CacheCleanup(LPTEMPORARY_CACHE Cache, U32 CurrentTime);

/************************************************************************/

#endif  // CACHE_H_INCLUDED
