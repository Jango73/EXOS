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
 * @brief Bridge generic cache flush callback to cluster cache callback.
 *
 * @param Data Cached payload (LPCLUSTER_CACHE_ENTRY).
 * @param Context Cluster cache descriptor pointer.
 * @return TRUE when flush succeeds, FALSE otherwise.
 */
static BOOL ClusterCacheFlushBridge(LPVOID Data, LPVOID Context) {
    LPCLUSTER_CACHE ClusterCache = (LPCLUSTER_CACHE)Context;
    LPCLUSTER_CACHE_ENTRY Entry = (LPCLUSTER_CACHE_ENTRY)Data;

    if (ClusterCache == NULL || Entry == NULL) return FALSE;
    if (ClusterCache->FlushCallback == NULL) return FALSE;

    return ClusterCache->FlushCallback(
        Entry->Owner,
        Entry->ClusterIndex,
        Entry->Data,
        Entry->DataSize,
        ClusterCache->FlushContext);
}

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
 * @brief Store cluster data in cache, optionally marked as dirty.
 *
 * @param ClusterCache Cache descriptor.
 * @param Owner Namespace key (usually filesystem instance pointer).
 * @param ClusterIndex Cluster index key.
 * @param Data Cluster payload.
 * @param DataSize Payload size in bytes.
 * @param MarkDirty TRUE to mark entry dirty after store, FALSE otherwise.
 * @return TRUE when operation succeeds, FALSE otherwise.
 */
static BOOL ClusterCacheStoreInternal(
    LPCLUSTER_CACHE ClusterCache, LPCVOID Owner, U64 ClusterIndex, LPCVOID Data, UINT DataSize, BOOL MarkDirty) {
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
    } else {
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
    }

    if (!MarkDirty) return TRUE;
    return CacheMarkEntryDirty(&ClusterCache->Cache, Entry);
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
    ClusterCache->FlushCallback = NULL;
    ClusterCache->FlushContext = NULL;
    CacheInit(&ClusterCache->Cache, Capacity);
    CacheSetWritePolicy(&ClusterCache->Cache, CACHE_WRITE_POLICY_READ_ONLY, NULL, NULL, NULL);
}

/***************************************************************************/

/**
 * @brief Configure write policy for a cluster cache descriptor.
 *
 * @param ClusterCache Cache descriptor.
 * @param WritePolicy One of CACHE_WRITE_POLICY_* values.
 * @param FlushCallback Flush callback used for dirty entries.
 * @param FlushContext Context pointer forwarded to FlushCallback.
 */
void ClusterCacheSetWritePolicy(
    LPCLUSTER_CACHE ClusterCache, U32 WritePolicy, CLUSTER_CACHE_FLUSH_CALLBACK FlushCallback, LPVOID FlushContext) {
    CACHE_FLUSH_CALLBACK CacheFlushCallback = NULL;

    if (ClusterCache == NULL) return;

    ClusterCache->FlushCallback = FlushCallback;
    ClusterCache->FlushContext = FlushContext;

    if (WritePolicy != CACHE_WRITE_POLICY_READ_ONLY) {
        CacheFlushCallback = ClusterCacheFlushBridge;
    }

    CacheSetWritePolicy(&ClusterCache->Cache, WritePolicy, CacheFlushCallback, NULL, ClusterCache);
}

/***************************************************************************/

/**
 * @brief Release all memory owned by a cluster cache descriptor.
 *
 * @param ClusterCache Cache descriptor to release.
 */
void ClusterCacheDeinit(LPCLUSTER_CACHE ClusterCache) {
    if (ClusterCache == NULL) return;

    ClusterCache->FlushCallback = NULL;
    ClusterCache->FlushContext = NULL;
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
    return ClusterCacheStoreInternal(ClusterCache, Owner, ClusterIndex, Data, DataSize, FALSE);
}

/***************************************************************************/

/**
 * @brief Store data in cache and mark it dirty using cache write policy.
 *
 * @param ClusterCache Cache descriptor.
 * @param Owner Namespace key (usually filesystem instance pointer).
 * @param ClusterIndex Cluster index key.
 * @param Data Cluster payload.
 * @param DataSize Payload size in bytes.
 * @return TRUE when operation succeeds, FALSE otherwise.
 */
BOOL ClusterCacheWrite(
    LPCLUSTER_CACHE ClusterCache, LPCVOID Owner, U64 ClusterIndex, LPCVOID Data, UINT DataSize) {
    return ClusterCacheStoreInternal(ClusterCache, Owner, ClusterIndex, Data, DataSize, TRUE);
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
 * @brief Flush one cluster entry identified by owner and cluster index.
 *
 * @param ClusterCache Cache descriptor.
 * @param Owner Namespace key (usually filesystem instance pointer).
 * @param ClusterIndex Cluster index key.
 * @param DataSize Entry payload size in bytes.
 * @return TRUE when flush succeeds or entry is already clean, FALSE otherwise.
 */
BOOL ClusterCacheFlushCluster(
    LPCLUSTER_CACHE ClusterCache, LPCVOID Owner, U64 ClusterIndex, UINT DataSize) {
    CLUSTER_CACHE_MATCH_CONTEXT Context;
    LPCLUSTER_CACHE_ENTRY Entry = NULL;

    if (ClusterCache == NULL || Owner == NULL || DataSize == 0) return FALSE;
    if (ClusterCache->Cache.Entries == NULL || ClusterCache->Cache.Capacity == 0) return FALSE;

    Context.Owner = Owner;
    Context.ClusterIndex = ClusterIndex;
    Context.DataSize = DataSize;

    Entry = (LPCLUSTER_CACHE_ENTRY)CacheFind(&ClusterCache->Cache, ClusterCacheMatcher, &Context);
    if (Entry == NULL) return FALSE;

    return CacheFlushEntry(&ClusterCache->Cache, Entry);
}

/***************************************************************************/

/**
 * @brief Flush all dirty cluster entries.
 *
 * @param ClusterCache Cache descriptor.
 * @return Number of flushed entries.
 */
UINT ClusterCacheFlushAll(LPCLUSTER_CACHE ClusterCache) {
    if (ClusterCache == NULL) return 0;
    if (ClusterCache->Cache.Entries == NULL || ClusterCache->Cache.Capacity == 0) return 0;

    return CacheFlushAllEntries(&ClusterCache->Cache);
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
