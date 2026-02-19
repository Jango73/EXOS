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

#ifndef CHUNKCACHE_H_INCLUDED
#define CHUNKCACHE_H_INCLUDED

/***************************************************************************/

#include "Base.h"
#include "utils/Cache.h"

/***************************************************************************/

#define CHUNK_CACHE_DEFAULT_CAPACITY 128
#define CHUNK_CACHE_DEFAULT_TTL_MS 8000

/***************************************************************************/

typedef struct tag_CHUNK_CACHE_ENTRY {
    LPCVOID Owner;
    U64 ChunkIndex;
    UINT DataSize;
    U8 Data[1];
} CHUNK_CACHE_ENTRY, *LPCHUNK_CACHE_ENTRY;

typedef struct tag_CHUNK_CACHE {
    CACHE Cache;
    UINT DefaultTimeToLive;
} CHUNK_CACHE, *LPCHUNK_CACHE;

/***************************************************************************/

void ChunkCacheInit(LPCHUNK_CACHE ChunkCache, UINT Capacity, UINT DefaultTimeToLive);

void ChunkCacheDeinit(LPCHUNK_CACHE ChunkCache);

BOOL ChunkCacheStore(LPCHUNK_CACHE ChunkCache,
                     LPCVOID Owner,
                     U64 ChunkIndex,
                     LPCVOID Data,
                     UINT DataSize);

BOOL ChunkCacheRead(LPCHUNK_CACHE ChunkCache,
                    LPCVOID Owner,
                    U64 ChunkIndex,
                    LPVOID Buffer,
                    UINT BufferSize);

void ChunkCacheCleanup(LPCHUNK_CACHE ChunkCache);

/***************************************************************************/

#endif
