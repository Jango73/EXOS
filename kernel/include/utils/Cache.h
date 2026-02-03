
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
#define CACHE_WRITE_POLICY_READ_ONLY 0
#define CACHE_WRITE_POLICY_WRITE_THROUGH 1
#define CACHE_WRITE_POLICY_WRITE_BACK 2

/************************************************************************/

typedef BOOL (*CACHE_FLUSH_CALLBACK)(LPVOID Data, LPVOID Context);
typedef void (*CACHE_RELEASE_CALLBACK)(LPVOID Data, BOOL Dirty, LPVOID Context);

/************************************************************************/

typedef struct tag_CACHE_ENTRY {
    LPVOID Data;
    UINT ExpirationTime;
    UINT TTL;
    UINT Score;
    BOOL Dirty;
    BOOL Valid;
} CACHE_ENTRY, *LPCACHE_ENTRY;

typedef struct tag_CACHE {
    LPCACHE_ENTRY Entries;
    UINT Capacity;
    UINT Count;
    U32 WritePolicy;
    CACHE_FLUSH_CALLBACK FlushCallback;
    CACHE_RELEASE_CALLBACK ReleaseCallback;
    LPVOID CallbackContext;
    MUTEX Mutex;
} CACHE, *LPCACHE;

/************************************************************************/

void CacheInit(LPCACHE Cache, UINT Capacity);
void CacheDeinit(LPCACHE Cache);
void CacheSetWritePolicy(
    LPCACHE Cache, U32 WritePolicy, CACHE_FLUSH_CALLBACK FlushCallback, CACHE_RELEASE_CALLBACK ReleaseCallback, LPVOID CallbackContext);
BOOL CacheAdd(LPCACHE Cache, LPVOID Data, UINT TTL_MS);
LPVOID CacheFind(LPCACHE Cache, BOOL (*Matcher)(LPVOID Data, LPVOID Context), LPVOID Context);
BOOL CacheMarkEntryDirty(LPCACHE Cache, LPVOID Data);
BOOL CacheFlushEntry(LPCACHE Cache, LPVOID Data);
UINT CacheFlushAllEntries(LPCACHE Cache);
void CacheCleanup(LPCACHE Cache, UINT CurrentTime);
LPCACHE_ENTRY CacheFindLowestScoreEntry(LPCACHE Cache);

/************************************************************************/

#endif  // CACHE_H_INCLUDED
