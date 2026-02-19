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

#include "utils/Compression.h"

#include "CoreString.h"

#define MINIZ_NO_STDIO
#define MINIZ_NO_TIME
#define MINIZ_NO_ARCHIVE_APIS
#define MINIZ_NO_ZLIB_COMPATIBLE_NAMES
#include "miniz.h"

/***************************************************************************/

/**
 * @brief Return miniz window bits from an EXOS compression format.
 * @param Format EXOS compression format.
 * @param WindowBits Output window bits accepted by miniz.
 * @return COMPRESSION_STATUS_OK on success.
 */
static U32 CompressionResolveWindowBits(U32 Format, I32* WindowBits) {
    if (WindowBits == NULL) {
        return COMPRESSION_STATUS_INVALID_ARGUMENT;
    }

    switch (Format) {
        case COMPRESSION_FORMAT_RAW_DEFLATE:
            *WindowBits = -MZ_DEFAULT_WINDOW_BITS;
            return COMPRESSION_STATUS_OK;
        case COMPRESSION_FORMAT_ZLIB:
            *WindowBits = MZ_DEFAULT_WINDOW_BITS;
            return COMPRESSION_STATUS_OK;
        default:
            return COMPRESSION_STATUS_INVALID_ARGUMENT;
    }
}

/***************************************************************************/

/**
 * @brief Convert a miniz status code to an EXOS compression status code.
 * @param Status Status code from miniz.
 * @return EXOS compression status code.
 */
static U32 CompressionMapStatus(I32 Status) {
    switch (Status) {
        case MZ_OK:
        case MZ_STREAM_END:
            return COMPRESSION_STATUS_OK;
        case MZ_DATA_ERROR:
            return COMPRESSION_STATUS_DATA_ERROR;
        case MZ_MEM_ERROR:
            return COMPRESSION_STATUS_MEMORY_ERROR;
        case MZ_BUF_ERROR:
            return COMPRESSION_STATUS_BUFFER_TOO_SMALL;
        case MZ_PARAM_ERROR:
            return COMPRESSION_STATUS_INVALID_ARGUMENT;
        default:
            return COMPRESSION_STATUS_INTERNAL_ERROR;
    }
}

/***************************************************************************/

/**
 * @brief Inflate a compressed memory buffer.
 * @param Source Compressed source bytes.
 * @param SourceLength Number of source bytes.
 * @param Destination Output buffer for decompressed bytes.
 * @param DestinationCapacity Capacity of output buffer.
 * @param DestinationLength Receives decompressed size when non-NULL.
 * @param Format Compression format selector.
 * @return EXOS compression status code.
 */
U32 CompressionInflate(const void* Source,
                      U32 SourceLength,
                      void* Destination,
                      U32 DestinationCapacity,
                      U32* DestinationLength,
                      U32 Format) {
    mz_stream Stream;
    I32 WindowBits;
    I32 Status;
    U32 Result;

    if (Source == NULL || Destination == NULL || SourceLength == 0 || DestinationCapacity == 0) {
        return COMPRESSION_STATUS_INVALID_ARGUMENT;
    }

    Result = CompressionResolveWindowBits(Format, &WindowBits);
    if (Result != COMPRESSION_STATUS_OK) {
        return Result;
    }

    memset(&Stream, 0, sizeof(Stream));
    Stream.next_in = (const unsigned char*)Source;
    Stream.avail_in = (mz_uint)SourceLength;
    Stream.next_out = (unsigned char*)Destination;
    Stream.avail_out = (mz_uint)DestinationCapacity;

    Status = mz_inflateInit2(&Stream, WindowBits);
    if (Status != MZ_OK) {
        return CompressionMapStatus(Status);
    }

    Status = mz_inflate(&Stream, MZ_FINISH);

    if (DestinationLength != NULL) {
        *DestinationLength = (U32)Stream.total_out;
    }

    mz_inflateEnd(&Stream);

    if (Status == MZ_STREAM_END) {
        return COMPRESSION_STATUS_OK;
    }

    return CompressionMapStatus(Status);
}

/***************************************************************************/

/**
 * @brief Deflate a plain memory buffer.
 * @param Source Plain source bytes.
 * @param SourceLength Number of source bytes.
 * @param Destination Output buffer for compressed bytes.
 * @param DestinationCapacity Capacity of output buffer.
 * @param DestinationLength Receives compressed size when non-NULL.
 * @param Format Compression format selector.
 * @param Level Compression level or COMPRESSION_LEVEL_DEFAULT.
 * @return EXOS compression status code.
 */
U32 CompressionDeflate(const void* Source,
                      U32 SourceLength,
                      void* Destination,
                      U32 DestinationCapacity,
                      U32* DestinationLength,
                      U32 Format,
                      U32 Level) {
    mz_stream Stream;
    I32 WindowBits;
    I32 Status;
    I32 EffectiveLevel;
    U32 Result;

    if (Source == NULL || Destination == NULL || SourceLength == 0 || DestinationCapacity == 0) {
        return COMPRESSION_STATUS_INVALID_ARGUMENT;
    }

    Result = CompressionResolveWindowBits(Format, &WindowBits);
    if (Result != COMPRESSION_STATUS_OK) {
        return Result;
    }

    if (Level == COMPRESSION_LEVEL_DEFAULT) {
        EffectiveLevel = MZ_DEFAULT_LEVEL;
    } else if (Level <= 10) {
        EffectiveLevel = (I32)Level;
    } else {
        return COMPRESSION_STATUS_INVALID_ARGUMENT;
    }

    memset(&Stream, 0, sizeof(Stream));
    Stream.next_in = (const unsigned char*)Source;
    Stream.avail_in = (mz_uint)SourceLength;
    Stream.next_out = (unsigned char*)Destination;
    Stream.avail_out = (mz_uint)DestinationCapacity;

    Status = mz_deflateInit2(&Stream,
                             EffectiveLevel,
                             MZ_DEFLATED,
                             WindowBits,
                             9,
                             MZ_DEFAULT_STRATEGY);
    if (Status != MZ_OK) {
        return CompressionMapStatus(Status);
    }

    Status = mz_deflate(&Stream, MZ_FINISH);

    if (DestinationLength != NULL) {
        *DestinationLength = (U32)Stream.total_out;
    }

    mz_deflateEnd(&Stream);

    if (Status == MZ_STREAM_END) {
        return COMPRESSION_STATUS_OK;
    }

    return CompressionMapStatus(Status);
}
