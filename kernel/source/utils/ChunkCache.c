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


    Generic chunk cache

\************************************************************************/

#include "utils/ChunkCache.h"

#include "Clock.h"
#include "CoreString.h"
#include "Heap.h"

/***************************************************************************/

typedef struct tag_CHUNK_CACHE_MATCH_CONTEXT {
    LPCVOID Owner;
    U64 ChunkIndex;
    UINT DataSize;
} CHUNK_CACHE_MATCH_CONTEXT, *LPCHUNK_CACHE_MATCH_CONTEXT;

/************************************************************************/

/**
 * @brief Match one chunk cache entry against owner/chunk/size keys.
 * @param Data Cache entry payload.
 * @param Context Match context.
 * @return TRUE when entry matches.
 */
static BOOL ChunkCacheMatcher(LPVOID Data, LPVOID Context) {
    LPCHUNK_CACHE_ENTRY Entry = (LPCHUNK_CACHE_ENTRY)Data;
    LPCHUNK_CACHE_MATCH_CONTEXT Match = (LPCHUNK_CACHE_MATCH_CONTEXT)Context;

    if (Entry == NULL || Match == NULL) {
        return FALSE;
    }

    if (Entry->Owner != Match->Owner) {
        return FALSE;
    }

    if (!U64_EQUAL(Entry->ChunkIndex, Match->ChunkIndex)) {
        return FALSE;
    }

    if (Entry->DataSize != Match->DataSize) {
        return FALSE;
    }

    return TRUE;
}

/************************************************************************/

/**
 * @brief Initialize a chunk cache descriptor.
 * @param ChunkCache Chunk cache descriptor.
 * @param Capacity Maximum number of entries.
 * @param DefaultTimeToLive Default entry lifetime in milliseconds.
 */
void ChunkCacheInit(LPCHUNK_CACHE ChunkCache, UINT Capacity, UINT DefaultTimeToLive) {
    if (ChunkCache == NULL) {
        return;
    }

    if (Capacity == 0) {
        Capacity = CHUNK_CACHE_DEFAULT_CAPACITY;
    }

    if (DefaultTimeToLive == 0) {
        DefaultTimeToLive = CHUNK_CACHE_DEFAULT_TTL_MS;
    }

    ChunkCache->DefaultTimeToLive = DefaultTimeToLive;
    CacheInit(&ChunkCache->Cache, Capacity);
    CacheSetWritePolicy(&ChunkCache->Cache, CACHE_WRITE_POLICY_READ_ONLY, NULL, NULL, NULL);
}

/************************************************************************/

/**
 * @brief Deinitialize a chunk cache descriptor.
 * @param ChunkCache Chunk cache descriptor.
 */
void ChunkCacheDeinit(LPCHUNK_CACHE ChunkCache) {
    if (ChunkCache == NULL) {
        return;
    }

    CacheDeinit(&ChunkCache->Cache);
    ChunkCache->DefaultTimeToLive = 0;
}

/************************************************************************/

/**
 * @brief Store chunk data in cache.
 * @param ChunkCache Chunk cache descriptor.
 * @param Owner Namespace owner key.
 * @param ChunkIndex Chunk index key.
 * @param Data Chunk payload.
 * @param DataSize Chunk payload size.
 * @return TRUE when data is stored.
 */
BOOL ChunkCacheStore(LPCHUNK_CACHE ChunkCache,
                     LPCVOID Owner,
                     U64 ChunkIndex,
                     LPCVOID Data,
                     UINT DataSize) {
    CHUNK_CACHE_MATCH_CONTEXT Context;
    LPCHUNK_CACHE_ENTRY Entry;
    UINT EntrySize;

    if (ChunkCache == NULL || Owner == NULL || Data == NULL || DataSize == 0) {
        return FALSE;
    }

    if (ChunkCache->Cache.Entries == NULL || ChunkCache->Cache.Capacity == 0) {
        return FALSE;
    }

    Context.Owner = Owner;
    Context.ChunkIndex = ChunkIndex;
    Context.DataSize = DataSize;

    Entry = (LPCHUNK_CACHE_ENTRY)CacheFind(&ChunkCache->Cache, ChunkCacheMatcher, &Context);
    if (Entry != NULL) {
        MemoryCopy(Entry->Data, Data, DataSize);
        return TRUE;
    }

    EntrySize = (UINT)(sizeof(CHUNK_CACHE_ENTRY) + DataSize - 1);
    Entry = (LPCHUNK_CACHE_ENTRY)KernelHeapAlloc(EntrySize);
    if (Entry == NULL) {
        return FALSE;
    }

    Entry->Owner = Owner;
    Entry->ChunkIndex = ChunkIndex;
    Entry->DataSize = DataSize;
    MemoryCopy(Entry->Data, Data, DataSize);

    if (!CacheAdd(&ChunkCache->Cache, Entry, ChunkCache->DefaultTimeToLive)) {
        KernelHeapFree(Entry);
        return FALSE;
    }

    return TRUE;
}

/************************************************************************/

/**
 * @brief Read chunk data from cache.
 * @param ChunkCache Chunk cache descriptor.
 * @param Owner Namespace owner key.
 * @param ChunkIndex Chunk index key.
 * @param Buffer Output buffer.
 * @param BufferSize Output buffer size.
 * @return TRUE when chunk is found.
 */
BOOL ChunkCacheRead(LPCHUNK_CACHE ChunkCache,
                    LPCVOID Owner,
                    U64 ChunkIndex,
                    LPVOID Buffer,
                    UINT BufferSize) {
    CHUNK_CACHE_MATCH_CONTEXT Context;
    LPCHUNK_CACHE_ENTRY Entry;

    if (ChunkCache == NULL || Owner == NULL || Buffer == NULL || BufferSize == 0) {
        return FALSE;
    }

    if (ChunkCache->Cache.Entries == NULL || ChunkCache->Cache.Capacity == 0) {
        return FALSE;
    }

    Context.Owner = Owner;
    Context.ChunkIndex = ChunkIndex;
    Context.DataSize = BufferSize;

    Entry = (LPCHUNK_CACHE_ENTRY)CacheFind(&ChunkCache->Cache, ChunkCacheMatcher, &Context);
    if (Entry == NULL) {
        return FALSE;
    }

    MemoryCopy(Buffer, Entry->Data, BufferSize);
    return TRUE;
}

/************************************************************************/

/**
 * @brief Cleanup expired chunk cache entries.
 * @param ChunkCache Chunk cache descriptor.
 */
void ChunkCacheCleanup(LPCHUNK_CACHE ChunkCache) {
    if (ChunkCache == NULL) {
        return;
    }

    if (ChunkCache->Cache.Entries == NULL || ChunkCache->Cache.Capacity == 0) {
        return;
    }

    CacheCleanup(&ChunkCache->Cache, GetSystemTime());
}
