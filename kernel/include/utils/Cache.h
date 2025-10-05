
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
} CACHE_ENTRY, *LPCACHE_ENTRY;

typedef struct {
    LPCACHE_ENTRY Entries;
    U32 Capacity;
    U32 Count;
    MUTEX Mutex;
} CACHE, *LPCACHE;

/************************************************************************/

void CacheInit(LPCACHE Cache, U32 Capacity);
void CacheDeinit(LPCACHE Cache);
BOOL CacheAdd(LPCACHE Cache, LPVOID Data, U32 TTL_MS);
LPVOID CacheFind(LPCACHE Cache, BOOL (*Matcher)(LPVOID Data, LPVOID Context), LPVOID Context);
void CacheCleanup(LPCACHE Cache, U32 CurrentTime);

/************************************************************************/

#endif  // CACHE_H_INCLUDED
