
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
#include "Memory.h"
#include "Mutex.h"

/************************************************************************/

static void CacheDecayScoresLocked(LPCACHE Cache) {
    for (UINT Index = 0; Index < Cache->Capacity; Index++) {
        if (Cache->Entries[Index].Valid && Cache->Entries[Index].Score > 0) {
            Cache->Entries[Index].Score--;
        }
    }
}

/************************************************************************/

static LPCACHE_ENTRY CacheFindLowestScoreEntryInternal(LPCACHE Cache) {
    LPCACHE_ENTRY LowestEntry = NULL;

    for (UINT Index = 0; Index < Cache->Capacity; Index++) {
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
void CacheInit(LPCACHE Cache, UINT Capacity) {
    UINT AllocationSize = (UINT)(Capacity * sizeof(CACHE_ENTRY));

    DEBUG(TEXT("[CacheInit] Cache=%p capacity=%u"), Cache, Capacity);
    DEBUG(TEXT("[CacheInit] EntrySize=%u allocationSize=%u"), (UINT)sizeof(CACHE_ENTRY), AllocationSize);

    Cache->Capacity = Capacity;
    Cache->Count = 0;
    Cache->Entries = (LPCACHE_ENTRY)KernelHeapAlloc(AllocationSize);
    Cache->Mutex = (MUTEX)EMPTY_MUTEX;

    DEBUG(TEXT("[CacheInit] Mutex=%p lockCount=%u"), &(Cache->Mutex), Cache->Mutex.Lock);
    DEBUG(TEXT("[CacheInit] Entries pointer=%p size=%u"), Cache->Entries, AllocationSize);

    if (Cache->Entries != NULL) {
        LPHEAPBLOCKHEADER AllocationHeader = (LPHEAPBLOCKHEADER)((LINEAR)Cache->Entries - sizeof(HEAPBLOCKHEADER));
        LINEAR AllocationStart = (LINEAR)Cache->Entries;
        LINEAR AllocationEnd = AllocationStart;

        if (AllocationSize > 0) {
            AllocationEnd = AllocationStart + (LINEAR)(AllocationSize - 1);
        }

        DEBUG(TEXT("[CacheInit] Allocation header=%p type=%x size=%u next=%p prev=%p"), AllocationHeader,
            AllocationHeader->TypeID, AllocationHeader->Size, AllocationHeader->Next, AllocationHeader->Prev);
        DEBUG(TEXT("[CacheInit] Allocation range start=%p end=%p"), (LPVOID)AllocationStart, (LPVOID)AllocationEnd);
    }

    LINEAR EntriesLinear = (LINEAR)Cache->Entries;
    LINEAR EntriesEnd = EntriesLinear;
    if (AllocationSize > 0) {
        EntriesEnd = EntriesLinear + (LINEAR)(AllocationSize - 1);
    }

    DEBUG(TEXT("[CacheInit] Entries range startValid=%u endValid=%u"), IsValidMemory(EntriesLinear),
          IsValidMemory(EntriesEnd));

    if (Cache->Entries == NULL) {
        ERROR(TEXT("[CacheInit] KernelHeapAlloc failed"));
        return;
    }

    DEBUG(TEXT("[CacheInit] Clearing %u entries"), Capacity);

    for (UINT Index = 0; Index < Capacity; Index++) {
        LINEAR EntryStart = (LINEAR)&Cache->Entries[Index];
        LINEAR EntryEnd = EntryStart + (LINEAR)(sizeof(CACHE_ENTRY) - 1);

        DEBUG(TEXT("[CacheInit] Clearing entry %u at %p startValid=%u endValid=%u"), Index,
              &Cache->Entries[Index], IsValidMemory(EntryStart), IsValidMemory(EntryEnd));
        Cache->Entries[Index].Data = NULL;
        Cache->Entries[Index].ExpirationTime = 0;
        Cache->Entries[Index].TTL = 0;
        Cache->Entries[Index].Score = 0;
        Cache->Entries[Index].Valid = FALSE;
    }

    DEBUG(TEXT("[CacheInit] Initialization complete"));
}

/************************************************************************/

/**
 * @brief Deinitialize a temporary cache.
 * @param Cache Cache structure to deinitialize
 */
void CacheDeinit(LPCACHE Cache) {
    DEBUG(TEXT("[CacheDeinit] Enter cache=%p entries=%p capacity=%u count=%u"), Cache, Cache->Entries,
        Cache->Capacity, Cache->Count);

    LockMutex(&Cache->Mutex, INFINITY);

    if (Cache->Entries) {
        for (UINT Index = 0; Index < Cache->Capacity; Index++) {
            if (Cache->Entries[Index].Valid && Cache->Entries[Index].Data) {
                DEBUG(TEXT("[CacheDeinit] Freeing data for entry %u data=%p"), Index, Cache->Entries[Index].Data);
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
BOOL CacheAdd(LPCACHE Cache, LPVOID Data, UINT TTL_MS) {
    DEBUG(TEXT("[CacheAdd] Cache=%p data=%p TTL=%u capacity=%u count=%u"), Cache, Data, TTL_MS,
        Cache->Capacity, Cache->Count);

    LockMutex(&Cache->Mutex, INFINITY);

    UINT CurrentTime = GetSystemTime();
    UINT FreeIndex = MAX_UINT;

    CacheDecayScoresLocked(Cache);

    // Find first free slot
    for (UINT Index = 0; Index < Cache->Capacity; Index++) {
        if (!Cache->Entries[Index].Valid) {
            DEBUG(TEXT("[CacheAdd] Found free entry at index %u"), Index);
            FreeIndex = Index;
            break;
        }

        if (CurrentTime >= Cache->Entries[Index].ExpirationTime) {
            DEBUG(TEXT("[CacheAdd] Expiring entry %u data=%p expiration=%u"), Index,
                Cache->Entries[Index].Data, Cache->Entries[Index].ExpirationTime);
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
            if (FreeIndex == MAX_UINT) {
                FreeIndex = Index;
            }
        }
    }

    if (FreeIndex == MAX_UINT) {
        LPCACHE_ENTRY LowestEntry = CacheFindLowestScoreEntryInternal(Cache);

        if (LowestEntry == NULL) {
            DEBUG(TEXT("[CacheAdd] Cache full and no entry available"));
            UnlockMutex(&Cache->Mutex);
            return FALSE;
        }

        if (LowestEntry->Data) {
            DEBUG(TEXT("[CacheAdd] Reusing lowest-score entry data=%p score=%u"), LowestEntry->Data,
                LowestEntry->Score);
            KernelHeapFree(LowestEntry->Data);
        }

        LowestEntry->Data = Data;
        LowestEntry->ExpirationTime = (UINT)(CurrentTime + TTL_MS);
        LowestEntry->TTL = TTL_MS;
        LowestEntry->Score = 1;

        DEBUG(TEXT("[CacheAdd] Replaced entry expiration=%u ttl=%u"), LowestEntry->ExpirationTime,
            LowestEntry->TTL);
        UnlockMutex(&Cache->Mutex);
        return TRUE;
    }

    DEBUG(TEXT("[CacheAdd] Writing data to index %u"), FreeIndex);

    Cache->Entries[FreeIndex].Data = Data;
    Cache->Entries[FreeIndex].ExpirationTime = (UINT)(CurrentTime + TTL_MS);
    Cache->Entries[FreeIndex].TTL = TTL_MS;
    Cache->Entries[FreeIndex].Score = 1;
    Cache->Entries[FreeIndex].Valid = TRUE;
    Cache->Count++;

    DEBUG(TEXT("[CacheAdd] Entry %u expiration=%u count=%u"), FreeIndex,
        Cache->Entries[FreeIndex].ExpirationTime, Cache->Count);

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
    DEBUG(TEXT("[CacheFind] Cache=%p matcher=%p context=%p"), Cache, Matcher, Context);

    LockMutex(&Cache->Mutex, INFINITY);

    UINT CurrentTime = GetSystemTime();

    for (UINT Index = 0; Index < Cache->Capacity; Index++) {
        if (Cache->Entries[Index].Valid) {
            // Check if expired
            if (CurrentTime >= Cache->Entries[Index].ExpirationTime) {
                DEBUG(TEXT("[CacheFind] Expiring entry %u data=%p expiration=%u"), Index,
                    Cache->Entries[Index].Data, Cache->Entries[Index].ExpirationTime);
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
                Cache->Entries[Index].ExpirationTime =
                    (UINT)(CurrentTime + Cache->Entries[Index].TTL);
                DEBUG(TEXT("[CacheFind] Match at index %u data=%p newScore=%u newExpiration=%u"), Index, Result,
                    Cache->Entries[Index].Score, Cache->Entries[Index].ExpirationTime);
                UnlockMutex(&Cache->Mutex);
                return Result;
            }

            if (Cache->Entries[Index].Score > 0) {
                Cache->Entries[Index].Score--;
                DEBUG(TEXT("[CacheFind] Decayed score for index %u newScore=%u"), Index,
                    Cache->Entries[Index].Score);
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
void CacheCleanup(LPCACHE Cache, UINT CurrentTime) {
    DEBUG(TEXT("[CacheCleanup] Cache=%p currentTime=%u"), Cache, CurrentTime);

    LockMutex(&Cache->Mutex, INFINITY);

    UINT RemovedCount = 0;

    CacheDecayScoresLocked(Cache);

    for (UINT Index = 0; Index < Cache->Capacity; Index++) {
        if (Cache->Entries[Index].Valid) {
            if (CurrentTime >= Cache->Entries[Index].ExpirationTime) {
                DEBUG(TEXT("[CacheCleanup] Removing entry %u data=%p expiration=%u"), Index,
                    Cache->Entries[Index].Data, Cache->Entries[Index].ExpirationTime);
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

    DEBUG(TEXT("[CacheFindLowestScoreEntry] Cache=%p"), Cache);

    LockMutex(&Cache->Mutex, INFINITY);

    Result = CacheFindLowestScoreEntryInternal(Cache);

    UnlockMutex(&Cache->Mutex);

    DEBUG(TEXT("[CacheFindLowestScoreEntry] Result=%p"), Result);

    return Result;
}
