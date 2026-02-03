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


    Generic Cluster Cache

\************************************************************************/

#include "drivers/ClusterCache.h"
#include "Clock.h"
#include "CoreString.h"
#include "Heap.h"

/***************************************************************************/

typedef struct tag_CLUSTER_CACHE_MATCH_CONTEXT {
    LPCVOID Owner;
    U64 ClusterIndex;
    UINT DataSize;
} CLUSTER_CACHE_MATCH_CONTEXT, *LPCLUSTER_CACHE_MATCH_CONTEXT;

/***************************************************************************/

/**
 * @brief Match one cache entry against owner/cluster/size keys.
 *
 * @param Data Cache payload pointer.
 * @param Context Match context pointer.
 * @return TRUE when entry matches, FALSE otherwise.
 */
static BOOL ClusterCacheMatcher(LPVOID Data, LPVOID Context) {
    LPCLUSTER_CACHE_ENTRY Entry = (LPCLUSTER_CACHE_ENTRY)Data;
    LPCLUSTER_CACHE_MATCH_CONTEXT Match = (LPCLUSTER_CACHE_MATCH_CONTEXT)Context;

    if (Entry == NULL || Match == NULL) return FALSE;
    if (Entry->Owner != Match->Owner) return FALSE;
    if (Entry->DataSize != Match->DataSize) return FALSE;
    if (!U64_EQUAL(Entry->ClusterIndex, Match->ClusterIndex)) return FALSE;

    return TRUE;
}

/***************************************************************************/

/**
 * @brief Initialize a cluster cache descriptor.
 *
 * @param ClusterCache Cache descriptor to initialize.
 * @param Capacity Maximum number of entries.
 * @param DefaultTimeToLive Default entry lifetime in milliseconds.
 */
void ClusterCacheInit(LPCLUSTER_CACHE ClusterCache, UINT Capacity, UINT DefaultTimeToLive) {
    if (ClusterCache == NULL) return;

    if (Capacity == 0) {
        Capacity = CLUSTER_CACHE_DEFAULT_CAPACITY;
    }

    if (DefaultTimeToLive == 0) {
        DefaultTimeToLive = CLUSTER_CACHE_DEFAULT_TTL_MS;
    }

    ClusterCache->DefaultTimeToLive = DefaultTimeToLive;
    CacheInit(&ClusterCache->Cache, Capacity);
}

/***************************************************************************/

/**
 * @brief Release all memory owned by a cluster cache descriptor.
 *
 * @param ClusterCache Cache descriptor to release.
 */
void ClusterCacheDeinit(LPCLUSTER_CACHE ClusterCache) {
    if (ClusterCache == NULL) return;

    CacheDeinit(&ClusterCache->Cache);
    ClusterCache->DefaultTimeToLive = 0;
}

/***************************************************************************/

/**
 * @brief Insert or refresh one cluster cache entry.
 *
 * @param ClusterCache Cache descriptor.
 * @param Owner Namespace key (usually filesystem instance pointer).
 * @param ClusterIndex Cluster index key.
 * @param Data Cluster payload.
 * @param DataSize Payload size in bytes.
 * @return TRUE when entry is stored, FALSE otherwise.
 */
BOOL ClusterCacheStore(
    LPCLUSTER_CACHE ClusterCache, LPCVOID Owner, U64 ClusterIndex, LPCVOID Data, UINT DataSize) {
    UINT EntrySize = 0;
    LPCLUSTER_CACHE_ENTRY Entry = NULL;
    CLUSTER_CACHE_MATCH_CONTEXT Context;

    if (ClusterCache == NULL || Owner == NULL || Data == NULL || DataSize == 0) return FALSE;
    if (ClusterCache->Cache.Entries == NULL || ClusterCache->Cache.Capacity == 0) return FALSE;

    Context.Owner = Owner;
    Context.ClusterIndex = ClusterIndex;
    Context.DataSize = DataSize;

    Entry = (LPCLUSTER_CACHE_ENTRY)CacheFind(&ClusterCache->Cache, ClusterCacheMatcher, &Context);
    if (Entry != NULL) {
        MemoryCopy(Entry->Data, Data, DataSize);
        return TRUE;
    }

    EntrySize = (UINT)(sizeof(CLUSTER_CACHE_ENTRY) + DataSize - 1);
    Entry = (LPCLUSTER_CACHE_ENTRY)KernelHeapAlloc(EntrySize);
    if (Entry == NULL) return FALSE;

    Entry->Owner = Owner;
    Entry->ClusterIndex = ClusterIndex;
    Entry->DataSize = DataSize;
    MemoryCopy(Entry->Data, Data, DataSize);

    if (!CacheAdd(&ClusterCache->Cache, Entry, ClusterCache->DefaultTimeToLive)) {
        KernelHeapFree(Entry);
        return FALSE;
    }

    return TRUE;
}

/***************************************************************************/

/**
 * @brief Read one cluster cache entry.
 *
 * @param ClusterCache Cache descriptor.
 * @param Owner Namespace key (usually filesystem instance pointer).
 * @param ClusterIndex Cluster index key.
 * @param Buffer Destination buffer.
 * @param BufferSize Destination buffer size in bytes.
 * @return TRUE when entry is found and copied, FALSE otherwise.
 */
BOOL ClusterCacheRead(
    LPCLUSTER_CACHE ClusterCache, LPCVOID Owner, U64 ClusterIndex, LPVOID Buffer, UINT BufferSize) {
    CLUSTER_CACHE_MATCH_CONTEXT Context;
    LPCLUSTER_CACHE_ENTRY Entry = NULL;

    if (ClusterCache == NULL || Owner == NULL || Buffer == NULL || BufferSize == 0) return FALSE;
    if (ClusterCache->Cache.Entries == NULL || ClusterCache->Cache.Capacity == 0) return FALSE;

    Context.Owner = Owner;
    Context.ClusterIndex = ClusterIndex;
    Context.DataSize = BufferSize;

    Entry = (LPCLUSTER_CACHE_ENTRY)CacheFind(&ClusterCache->Cache, ClusterCacheMatcher, &Context);
    if (Entry == NULL) return FALSE;

    MemoryCopy(Buffer, Entry->Data, BufferSize);
    return TRUE;
}

/***************************************************************************/

/**
 * @brief Remove expired entries from a cluster cache descriptor.
 *
 * @param ClusterCache Cache descriptor.
 */
void ClusterCacheCleanup(LPCLUSTER_CACHE ClusterCache) {
    if (ClusterCache == NULL) return;
    if (ClusterCache->Cache.Entries == NULL || ClusterCache->Cache.Capacity == 0) return;

    CacheCleanup(&ClusterCache->Cache, GetSystemTime());
}

/***************************************************************************/
