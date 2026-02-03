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

#ifndef CLUSTERCACHE_H_INCLUDED
#define CLUSTERCACHE_H_INCLUDED

/***************************************************************************/

#include "Base.h"
#include "utils/Cache.h"

/***************************************************************************/

#define CLUSTER_CACHE_DEFAULT_CAPACITY 128
#define CLUSTER_CACHE_DEFAULT_TTL_MS 4000

/***************************************************************************/

typedef struct tag_CLUSTER_CACHE_ENTRY {
    LPCVOID Owner;
    U64 ClusterIndex;
    UINT DataSize;
    U8 Data[1];
} CLUSTER_CACHE_ENTRY, *LPCLUSTER_CACHE_ENTRY;

typedef BOOL (*CLUSTER_CACHE_FLUSH_CALLBACK)(
    LPCVOID Owner, U64 ClusterIndex, LPCVOID Data, UINT DataSize, LPVOID Context);

typedef struct tag_CLUSTER_CACHE {
    CACHE Cache;
    UINT DefaultTimeToLive;
    CLUSTER_CACHE_FLUSH_CALLBACK FlushCallback;
    LPVOID FlushContext;
} CLUSTER_CACHE, *LPCLUSTER_CACHE;

/***************************************************************************/

/**
 * @brief Initialize a cluster cache descriptor.
 *
 * @param ClusterCache Cache descriptor to initialize.
 * @param Capacity Maximum number of entries.
 * @param DefaultTimeToLive Default entry lifetime in milliseconds.
 */
void ClusterCacheInit(LPCLUSTER_CACHE ClusterCache, UINT Capacity, UINT DefaultTimeToLive);

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
    LPCLUSTER_CACHE ClusterCache, U32 WritePolicy, CLUSTER_CACHE_FLUSH_CALLBACK FlushCallback, LPVOID FlushContext);

/***************************************************************************/

/**
 * @brief Release all memory owned by a cluster cache descriptor.
 *
 * @param ClusterCache Cache descriptor to release.
 */
void ClusterCacheDeinit(LPCLUSTER_CACHE ClusterCache);

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
    LPCLUSTER_CACHE ClusterCache, LPCVOID Owner, U64 ClusterIndex, LPCVOID Data, UINT DataSize);

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
    LPCLUSTER_CACHE ClusterCache, LPCVOID Owner, U64 ClusterIndex, LPCVOID Data, UINT DataSize);

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
    LPCLUSTER_CACHE ClusterCache, LPCVOID Owner, U64 ClusterIndex, LPVOID Buffer, UINT BufferSize);

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
    LPCLUSTER_CACHE ClusterCache, LPCVOID Owner, U64 ClusterIndex, UINT DataSize);

/***************************************************************************/

/**
 * @brief Flush all dirty cluster entries.
 *
 * @param ClusterCache Cache descriptor.
 * @return Number of flushed entries.
 */
UINT ClusterCacheFlushAll(LPCLUSTER_CACHE ClusterCache);

/***************************************************************************/

/**
 * @brief Remove expired entries from a cluster cache descriptor.
 *
 * @param ClusterCache Cache descriptor.
 */
void ClusterCacheCleanup(LPCLUSTER_CACHE ClusterCache);

/***************************************************************************/

#endif  // CLUSTERCACHE_H_INCLUDED
