
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

#include "Base.h"
#include "utils/Cache.h"
#include "Clock.h"
#include "Heap.h"
#include "Log.h"
#include "Mutex.h"

/************************************************************************/

static void CacheDecayScoresLocked(LPCACHE Cache) {
    for (U32 Index = 0; Index < Cache->Capacity; Index++) {
        if (Cache->Entries[Index].Valid && Cache->Entries[Index].Score > 0) {
            Cache->Entries[Index].Score--;
        }
    }
}

/************************************************************************/

static LPCACHE_ENTRY CacheFindLowestScoreEntryInternal(LPCACHE Cache) {
    LPCACHE_ENTRY LowestEntry = NULL;

    for (U32 Index = 0; Index < Cache->Capacity; Index++) {
        if (!Cache->Entries[Index].Valid) {
            continue;
        }

        if (LowestEntry == NULL || Cache->Entries[Index].Score < LowestEntry->Score) {
            LowestEntry = &Cache->Entries[Index];
        }
    }

    return LowestEntry;
}

/************************************************************************/

/**
 * @brief Initialize a temporary cache.
 * @param Cache Cache structure to initialize
 * @param Capacity Maximum number of entries
 */
void CacheInit(LPCACHE Cache, U32 Capacity) {
    DEBUG(TEXT("[CacheInit] Capacity: %u"), Capacity);

    Cache->Capacity = Capacity;
    Cache->Count = 0;
    Cache->Entries = (LPCACHE_ENTRY)KernelHeapAlloc(Capacity * sizeof(CACHE_ENTRY));
    Cache->Mutex = (MUTEX)EMPTY_MUTEX;

    for (U32 Index = 0; Index < Capacity; Index++) {
        Cache->Entries[Index].Data = NULL;
        Cache->Entries[Index].ExpirationTime = 0;
        Cache->Entries[Index].TTL = 0;
        Cache->Entries[Index].Score = 0;
        Cache->Entries[Index].Valid = FALSE;
    }
}

/************************************************************************/

/**
 * @brief Deinitialize a temporary cache.
 * @param Cache Cache structure to deinitialize
 */
void CacheDeinit(LPCACHE Cache) {
    DEBUG(TEXT("[CacheDeinit] Enter"));

    LockMutex(&Cache->Mutex, INFINITY);

    if (Cache->Entries) {
        for (U32 Index = 0; Index < Cache->Capacity; Index++) {
            if (Cache->Entries[Index].Valid && Cache->Entries[Index].Data) {
                KernelHeapFree(Cache->Entries[Index].Data);
            }
            Cache->Entries[Index].Data = NULL;
            Cache->Entries[Index].ExpirationTime = 0;
            Cache->Entries[Index].TTL = 0;
            Cache->Entries[Index].Score = 0;
            Cache->Entries[Index].Valid = FALSE;
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
BOOL CacheAdd(LPCACHE Cache, LPVOID Data, U32 TTL_MS) {
    LockMutex(&Cache->Mutex, INFINITY);

    U32 CurrentTime = GetSystemTime();
    U32 FreeIndex = MAX_U32;

    CacheDecayScoresLocked(Cache);

    // Find first free slot
    for (U32 Index = 0; Index < Cache->Capacity; Index++) {
        if (!Cache->Entries[Index].Valid) {
            FreeIndex = Index;
            break;
        }

        if (CurrentTime >= Cache->Entries[Index].ExpirationTime) {
            if (Cache->Entries[Index].Data) {
                KernelHeapFree(Cache->Entries[Index].Data);
            }
            Cache->Entries[Index].Data = NULL;
            Cache->Entries[Index].ExpirationTime = 0;
            Cache->Entries[Index].TTL = 0;
            Cache->Entries[Index].Score = 0;
            Cache->Entries[Index].Valid = FALSE;
            if (Cache->Count > 0) {
                Cache->Count--;
            }
            if (FreeIndex == MAX_U32) {
                FreeIndex = Index;
            }
        }
    }

    if (FreeIndex == MAX_U32) {
        LPCACHE_ENTRY LowestEntry = CacheFindLowestScoreEntryInternal(Cache);

        if (LowestEntry == NULL) {
            DEBUG(TEXT("[CacheAdd] Cache full and no entry available"));
            UnlockMutex(&Cache->Mutex);
            return FALSE;
        }

        if (LowestEntry->Data) {
            KernelHeapFree(LowestEntry->Data);
        }

        LowestEntry->Data = Data;
        LowestEntry->ExpirationTime = CurrentTime + TTL_MS;
        LowestEntry->TTL = TTL_MS;
        LowestEntry->Score = 1;

        UnlockMutex(&Cache->Mutex);
        return TRUE;
    }

    Cache->Entries[FreeIndex].Data = Data;
    Cache->Entries[FreeIndex].ExpirationTime = CurrentTime + TTL_MS;
    Cache->Entries[FreeIndex].TTL = TTL_MS;
    Cache->Entries[FreeIndex].Score = 1;
    Cache->Entries[FreeIndex].Valid = TRUE;
    Cache->Count++;

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
LPVOID CacheFind(LPCACHE Cache, BOOL (*Matcher)(LPVOID Data, LPVOID Context), LPVOID Context) {
    LockMutex(&Cache->Mutex, INFINITY);

    U32 CurrentTime = GetSystemTime();

    for (U32 Index = 0; Index < Cache->Capacity; Index++) {
        if (Cache->Entries[Index].Valid) {
            // Check if expired
            if (CurrentTime >= Cache->Entries[Index].ExpirationTime) {
                if (Cache->Entries[Index].Data) {
                    KernelHeapFree(Cache->Entries[Index].Data);
                }
                Cache->Entries[Index].Data = NULL;
                Cache->Entries[Index].ExpirationTime = 0;
                Cache->Entries[Index].TTL = 0;
                Cache->Entries[Index].Score = 0;
                Cache->Entries[Index].Valid = FALSE;
                if (Cache->Count > 0) {
                    Cache->Count--;
                }
                continue;
            }

            if (Matcher(Cache->Entries[Index].Data, Context)) {
                LPVOID Result = Cache->Entries[Index].Data;
                Cache->Entries[Index].Score++;
                Cache->Entries[Index].ExpirationTime = CurrentTime + Cache->Entries[Index].TTL;
                DEBUG(TEXT("[CacheFind] Found at index %u"), Index);
                UnlockMutex(&Cache->Mutex);
                return Result;
            }

            if (Cache->Entries[Index].Score > 0) {
                Cache->Entries[Index].Score--;
            }
        }
    }

    UnlockMutex(&Cache->Mutex);
    return NULL;
}

/************************************************************************/

/**
 * @brief Cleanup expired entries from cache.
 * @param Cache Cache structure
 * @param CurrentTime Current system time
 */
void CacheCleanup(LPCACHE Cache, U32 CurrentTime) {
    LockMutex(&Cache->Mutex, INFINITY);

    U32 RemovedCount = 0;

    CacheDecayScoresLocked(Cache);

    for (U32 Index = 0; Index < Cache->Capacity; Index++) {
        if (Cache->Entries[Index].Valid) {
            if (CurrentTime >= Cache->Entries[Index].ExpirationTime) {
                if (Cache->Entries[Index].Data) {
                    KernelHeapFree(Cache->Entries[Index].Data);
                }
                Cache->Entries[Index].Data = NULL;
                Cache->Entries[Index].Valid = FALSE;
                Cache->Count--;
                Cache->Entries[Index].ExpirationTime = 0;
                Cache->Entries[Index].TTL = 0;
                Cache->Entries[Index].Score = 0;
                RemovedCount++;
            }
        }
    }

    if (RemovedCount > 0) {
        DEBUG(TEXT("[CacheCleanup] Removed %u expired entries"), RemovedCount);
    }

    UnlockMutex(&Cache->Mutex);
}

/************************************************************************/

LPCACHE_ENTRY CacheFindLowestScoreEntry(LPCACHE Cache) {
    LPCACHE_ENTRY Result;

    LockMutex(&Cache->Mutex, INFINITY);

    Result = CacheFindLowestScoreEntryInternal(Cache);

    UnlockMutex(&Cache->Mutex);

    return Result;
}
