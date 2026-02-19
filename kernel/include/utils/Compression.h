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


    Compression helpers

\************************************************************************/

#ifndef COMPRESSION_H_INCLUDED
#define COMPRESSION_H_INCLUDED

#include "Base.h"

#ifdef __cplusplus
extern "C" {
#endif

/***************************************************************************/

#define COMPRESSION_FORMAT_RAW_DEFLATE 1
#define COMPRESSION_FORMAT_ZLIB 2

#define COMPRESSION_LEVEL_DEFAULT MAX_U32

#define COMPRESSION_STATUS_OK 0
#define COMPRESSION_STATUS_INVALID_ARGUMENT 1
#define COMPRESSION_STATUS_BUFFER_TOO_SMALL 2
#define COMPRESSION_STATUS_DATA_ERROR 3
#define COMPRESSION_STATUS_MEMORY_ERROR 4
#define COMPRESSION_STATUS_INTERNAL_ERROR 5

/***************************************************************************/

U32 CompressionInflate(const void* Source,
                      U32 SourceLength,
                      void* Destination,
                      U32 DestinationCapacity,
                      U32* DestinationLength,
                      U32 Format);

U32 CompressionDeflate(const void* Source,
                      U32 SourceLength,
                      void* Destination,
                      U32 DestinationCapacity,
                      U32* DestinationLength,
                      U32 Format,
                      U32 Level);

/***************************************************************************/

#ifdef __cplusplus
}
#endif

#endif
