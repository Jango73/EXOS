
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

#include "../include/Base.h"
#include "../include/Cache.h"
#include "../include/Clock.h"
#include "../include/Heap.h"
#include "../include/Log.h"
#include "../include/Mutex.h"

/************************************************************************/

/**
 * @brief Initialize a temporary cache.
 * @param Cache Cache structure to initialize
 * @param Capacity Maximum number of entries
 */
void CacheInit(LPTEMPORARY_CACHE Cache, U32 Capacity) {
    DEBUG(TEXT("[CacheInit] Capacity: %u"), Capacity);

    Cache->Capacity = Capacity;
    Cache->Count = 0;
    Cache->Entries = (LPTEMPORARY_CACHE_ENTRY)KernelHeapAlloc(Capacity * sizeof(TEMPORARY_CACHE_ENTRY));
    Cache->Mutex = (MUTEX)EMPTY_MUTEX;

    for (U32 Index = 0; Index < Capacity; Index++) {
        Cache->Entries[Index].Data = NULL;
        Cache->Entries[Index].ExpirationTime = 0;
        Cache->Entries[Index].Valid = FALSE;
    }
}

/************************************************************************/

/**
 * @brief Deinitialize a temporary cache.
 * @param Cache Cache structure to deinitialize
 */
void CacheDeinit(LPTEMPORARY_CACHE Cache) {
    DEBUG(TEXT("[CacheDeinit] Enter"));

    LockMutex(&Cache->Mutex, INFINITY);

    if (Cache->Entries) {
        for (U32 Index = 0; Index < Cache->Capacity; Index++) {
            if (Cache->Entries[Index].Valid && Cache->Entries[Index].Data) {
                KernelHeapFree(Cache->Entries[Index].Data);
            }
        }
        KernelHeapFree(Cache->Entries);
        Cache->Entries = NULL;
    }

    UnlockMutex(&Cache->Mutex);
}

/************************************************************************/

/**
 * @brief Add an entry to the cache with TTL.
 * @param Cache Cache structure
 * @param Data Pointer to data to store (will be managed by caller)
 * @param TTL_MS Time to live in milliseconds
 * @return TRUE if added successfully, FALSE otherwise
 */
BOOL CacheAdd(LPTEMPORARY_CACHE Cache, LPVOID Data, U32 TTL_MS) {
    DEBUG(TEXT("[CacheAdd] TTL: %u ms, Data=%x"), TTL_MS, Data);

    LockMutex(&Cache->Mutex, INFINITY);

    U32 CurrentTime = GetSystemTime();
    U32 FreeIndex = MAX_U32;

    // Find first free slot
    for (U32 Index = 0; Index < Cache->Capacity; Index++) {
        if (!Cache->Entries[Index].Valid) {
            FreeIndex = Index;
            break;
        }
    }

    if (FreeIndex == MAX_U32) {
        DEBUG(TEXT("[CacheAdd] Cache full"));
        UnlockMutex(&Cache->Mutex);
        return FALSE;
    }

    Cache->Entries[FreeIndex].Data = Data;
    Cache->Entries[FreeIndex].ExpirationTime = CurrentTime + TTL_MS;
    Cache->Entries[FreeIndex].Valid = TRUE;
    Cache->Count++;

    DEBUG(TEXT("[CacheAdd] Added at index %u, expires at %u"), FreeIndex, Cache->Entries[FreeIndex].ExpirationTime);

    UnlockMutex(&Cache->Mutex);
    return TRUE;
}

/************************************************************************/

/**
 * @brief Find an entry in the cache using a matcher function.
 * @param Cache Cache structure
 * @param Matcher Function to match entries
 * @param Context Context passed to matcher
 * @return Pointer to data if found, NULL otherwise
 */
LPVOID CacheFind(LPTEMPORARY_CACHE Cache, BOOL (*Matcher)(LPVOID Data, LPVOID Context), LPVOID Context) {
    DEBUG(TEXT("[CacheFind] Enter"));

    LockMutex(&Cache->Mutex, INFINITY);

    U32 CurrentTime = GetSystemTime();

    for (U32 Index = 0; Index < Cache->Capacity; Index++) {
        if (Cache->Entries[Index].Valid) {
            // Check if expired
            if (CurrentTime >= Cache->Entries[Index].ExpirationTime) {
                continue;
            }

            if (Matcher(Cache->Entries[Index].Data, Context)) {
                LPVOID Result = Cache->Entries[Index].Data;
                DEBUG(TEXT("[CacheFind] Found at index %u"), Index);
                UnlockMutex(&Cache->Mutex);
                return Result;
            }
        }
    }

    DEBUG(TEXT("[CacheFind] Not found"));
    UnlockMutex(&Cache->Mutex);
    return NULL;
}

/************************************************************************/

/**
 * @brief Cleanup expired entries from cache.
 * @param Cache Cache structure
 * @param CurrentTime Current system time
 */
void CacheCleanup(LPTEMPORARY_CACHE Cache, U32 CurrentTime) {
    LockMutex(&Cache->Mutex, INFINITY);

    U32 RemovedCount = 0;

    for (U32 Index = 0; Index < Cache->Capacity; Index++) {
        if (Cache->Entries[Index].Valid) {
            if (CurrentTime >= Cache->Entries[Index].ExpirationTime) {
                if (Cache->Entries[Index].Data) {
                    KernelHeapFree(Cache->Entries[Index].Data);
                }
                Cache->Entries[Index].Data = NULL;
                Cache->Entries[Index].Valid = FALSE;
                Cache->Count--;
                RemovedCount++;
            }
        }
    }

    if (RemovedCount > 0) {
        DEBUG(TEXT("[CacheCleanup] Removed %u expired entries"), RemovedCount);
    }

    UnlockMutex(&Cache->Mutex);
}
