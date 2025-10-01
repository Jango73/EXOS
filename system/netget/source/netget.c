
/************************************************************************\

    EXOS Network Download Utility
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


    NetGet - HTTP client for downloading files from the web

\************************************************************************/

#include "../../../runtime/include/exos.h"
#include "../../../runtime/include/http.h"
#include <ctype.h>

/************************************************************************/

typedef enum {
    CHUNK_STATE_READ_SIZE = 0,
    CHUNK_STATE_READ_DATA,
    CHUNK_STATE_READ_DATA_CRLF,
    CHUNK_STATE_READ_TRAILERS,
    CHUNK_STATE_FINISHED
} HTTP_CHUNK_STATE;

typedef struct {
    HTTP_CHUNK_STATE State;
    unsigned int CurrentChunkSize;
    unsigned int BytesRemainingInChunk;
    unsigned int TotalBytesWritten;
    char SizeBuffer[32];
    unsigned int SizeBufferUsed;
    unsigned int CrLfBytesNeeded;
    int PendingCR;
    int TrailerLineHasData;
} HTTP_CHUNK_PARSER;

/************************************************************************/

static void HTTP_ChunkParserInit(HTTP_CHUNK_PARSER* parser) {
    if (!parser) {
        return;
    }

    parser->State = CHUNK_STATE_READ_SIZE;
    parser->CurrentChunkSize = 0;
    parser->BytesRemainingInChunk = 0;
    parser->TotalBytesWritten = 0;
    parser->SizeBufferUsed = 0;
    parser->CrLfBytesNeeded = 0;
    parser->PendingCR = 0;
    parser->TrailerLineHasData = 0;
}

/************************************************************************/

static unsigned int HTTP_ParseChunkSizeValue(const char* value) {
    unsigned int result = 0;
    const char* p = value;

    while (*p != '\0') {
        char c = *p;
        unsigned int digit;

        if (c >= '0' && c <= '9') {
            digit = (unsigned int)(c - '0');
        } else if (c >= 'a' && c <= 'f') {
            digit = (unsigned int)(c - 'a' + 10U);
        } else if (c >= 'A' && c <= 'F') {
            digit = (unsigned int)(c - 'A' + 10U);
        } else {
            break;
        }

        result = (result << 4) | digit;
        p++;
    }

    return result;
}

/************************************************************************/

static int HTTP_ChunkParserProcess(HTTP_CHUNK_PARSER* parser,
                                   const unsigned char* data,
                                   unsigned int length,
                                   FILE* file,
                                   unsigned int* bytesWritten) {
    unsigned int offset = 0;
    unsigned int writtenThisCall = 0;

    if (!parser || !data || length == 0 || !file) {
        if (bytesWritten) {
            *bytesWritten = 0;
        }
        return HTTP_SUCCESS;
    }

    while (offset < length && parser->State != CHUNK_STATE_FINISHED) {
        switch (parser->State) {
            case CHUNK_STATE_READ_SIZE: {
                unsigned char c = data[offset++];

                if (c == '\n') {
                    if (parser->SizeBufferUsed > 0 &&
                        parser->SizeBuffer[parser->SizeBufferUsed - 1] == '\r') {
                        parser->SizeBufferUsed--;
                    }
                    parser->SizeBuffer[parser->SizeBufferUsed] = '\0';

                    if (parser->SizeBufferUsed == 0) {
                        printf("Error: Invalid chunk size line\n");
                        return HTTP_ERROR_PROTOCOL_ERROR;
                    }

                    char* semicolon = strchr(parser->SizeBuffer, ';');
                    if (semicolon) {
                        *semicolon = '\0';
                    }

                    parser->CurrentChunkSize = HTTP_ParseChunkSizeValue(parser->SizeBuffer);
                    parser->BytesRemainingInChunk = parser->CurrentChunkSize;
                    parser->SizeBufferUsed = 0;

                    if (parser->CurrentChunkSize == 0U) {
                        parser->State = CHUNK_STATE_READ_TRAILERS;
                        parser->PendingCR = 0;
                        parser->TrailerLineHasData = 0;
                    } else {
                        parser->State = CHUNK_STATE_READ_DATA;
                    }
                } else {
                    if (parser->SizeBufferUsed >= sizeof(parser->SizeBuffer) - 1U) {
                        printf("Error: Chunk size line too long\n");
                        return HTTP_ERROR_PROTOCOL_ERROR;
                    }
                    parser->SizeBuffer[parser->SizeBufferUsed++] = (char)c;
                }
                break;
            }

            case CHUNK_STATE_READ_DATA: {
                unsigned int available = length - offset;
                unsigned int toWrite = parser->BytesRemainingInChunk;

                if (toWrite > available) {
                    toWrite = available;
                }

                if (toWrite > 0U) {
                    if (SaveDataChunkToFile(file, data + offset, toWrite) != 0) {
                        return HTTP_ERROR_MEMORY_ERROR;
                    }
                    offset += toWrite;
                    parser->BytesRemainingInChunk -= toWrite;
                    parser->TotalBytesWritten += toWrite;
                    writtenThisCall += toWrite;
                }

                if (parser->BytesRemainingInChunk == 0U) {
                    parser->State = CHUNK_STATE_READ_DATA_CRLF;
                    parser->CrLfBytesNeeded = 2U;
                }
                break;
            }

            case CHUNK_STATE_READ_DATA_CRLF: {
                unsigned char c = data[offset++];

                if ((parser->CrLfBytesNeeded == 2U && c != '\r') ||
                    (parser->CrLfBytesNeeded == 1U && c != '\n')) {
                    printf("Error: Invalid chunk delimiter\n");
                    return HTTP_ERROR_PROTOCOL_ERROR;
                }

                parser->CrLfBytesNeeded--;
                if (parser->CrLfBytesNeeded == 0U) {
                    parser->State = CHUNK_STATE_READ_SIZE;
                }
                break;
            }

            case CHUNK_STATE_READ_TRAILERS: {
                unsigned char c = data[offset++];

                if (parser->PendingCR) {
                    if (c != '\n') {
                        printf("Error: Invalid trailer line ending\n");
                        return HTTP_ERROR_PROTOCOL_ERROR;
                    }

                    parser->PendingCR = 0;
                    if (parser->TrailerLineHasData == 0) {
                        parser->State = CHUNK_STATE_FINISHED;
                    } else {
                        parser->TrailerLineHasData = 0;
                    }
                    break;
                }

                if (c == '\r') {
                    parser->PendingCR = 1;
                } else {
                    parser->TrailerLineHasData = 1;
                }
                break;
            }

            case CHUNK_STATE_FINISHED:
            default:
                break;
        }
    }

    if (bytesWritten) {
        *bytesWritten = writtenThisCall;
    }

    return HTTP_SUCCESS;
}

/************************************************************************/

static int HTTP_StringContainsChunked(const char* value) {
    const char* p = value;

    while (p && *p) {
        const char* candidate = p;
        const char* keyword = "chunked";
        while (*candidate && *keyword &&
               (char)tolower((unsigned char)*candidate) == (char)tolower((unsigned char)*keyword)) {
            candidate++;
            keyword++;
        }

        if (*keyword == '\0') {
            return 1;
        }

        p++;
    }

    return 0;
}

/************************************************************************/

/************************************************************************/

/**
 * @brief Print command line usage information
 */
static void PrintUsage(void) {
    printf("Usage: netget <URL> [output_file]\n");
    printf("  URL         : HTTP URL to download (e.g., http://192.168.1.100/file.txt)\n");
    printf("  output_file : Optional output filename (default: extracted from URL)\n");
}

/************************************************************************/

/**
 * @brief Extract filename from URL path
 * @param path The URL path string
 * @return Pointer to the filename portion or "index.html" if none found
 */
const char* ExtractFilename(const char* path) {
    const char* filename = path;
    const char* p = path;

    // Find the last '/' in the path
    while (*p) {
        if (*p == '/') {
            filename = p + 1;
        }
        p++;
    }

    // If no filename found, use default
    if (*filename == '\0') {
        return "index.html";
    }

    return filename;
}

/************************************************************************/

/**
 * @brief Save a data chunk to file
 * @param file The file handle to write to
 * @param data The data buffer to write
 * @param dataLength The number of bytes to write
 * @return 0 on success, -1 on error
 */
int SaveDataChunkToFile(FILE* file, const unsigned char* data, unsigned int dataLength) {
    size_t written;

    if (!file || !data || dataLength == 0) {
        return 0;
    }

    written = fwrite(data, 1, dataLength, file);
    if (written != dataLength) {
        printf("Error: Could not write chunk to file (wrote %u of %u bytes)\n",
               (unsigned int)written, dataLength);
        return -1;
    }

    return 0;
}

/************************************************************************/

/**
 * @brief Receive HTTP response progressively and write to file
 * @param connection The HTTP connection
 * @param filename The output filename to save the response body
 * @return HTTP_SUCCESS on success, error code otherwise
 */
int HTTP_ReceiveResponseProgressive(HTTP_CONNECTION* connection, const char* filename) {
    char buffer[1024];
    int received;
    int totalReceived = 0;
    char* headerEnd;
    char* statusLine;
    FILE* file = NULL;
    int headersParsed = 0;
    unsigned int contentLength = 0;
    unsigned short statusCode = 0;
    char version[16] = {0};
    unsigned int bodyBytesReceived = 0;
    unsigned int headerLength = 0;
    int isChunked = 0;
    HTTP_CHUNK_PARSER chunkParser;

    // Buffer to accumulate headers
    char headerBuffer[4096] = {0};
    unsigned int headerBufferUsed = 0;

    const unsigned int receiveTimeoutMs = 5000;
    const unsigned int pollIntervalMs = 10;
    unsigned int idleTimeMs = 0;

    if (!connection || !filename) {
        return HTTP_ERROR_INVALID_RESPONSE;
    }

    // Reset receive buffer
    connection->ReceiveBufferUsed = 0;

    // Receive response progressively
    while (1) {
        received = recv(connection->SocketHandle, buffer, sizeof(buffer), 0);

        if (received < 0) {
            if (received == SOCKET_ERROR_WOULDBLOCK) {
                Sleep(pollIntervalMs);
                idleTimeMs += pollIntervalMs;

                if (idleTimeMs >= receiveTimeoutMs) {
                    printf("\nError: recv() timeout waiting for server response\n");
                    if (file) fclose(file);
                    return HTTP_ERROR_TIMEOUT;
                }

                continue;
            }

            if (received == SOCKET_ERROR_TIMEOUT) {
                printf("\nError: recv() timed out\n");
                if (file) fclose(file);
                return HTTP_ERROR_TIMEOUT;
            }

            printf("\nError: recv() failed - ");
            if (received == -1) {
                printf("timeout waiting for server response\n");
            } else {
                printf("connection error (code %d)\n", received);
            }
            if (file) fclose(file);
            return HTTP_ERROR_CONNECTION_FAILED;
        } else if (received == 0) {
            // Connection closed by peer
            if (!headersParsed) {
                printf("Error: Connection closed before headers received\n");
                if (file) fclose(file);
                return HTTP_ERROR_CONNECTION_FAILED;
            }
            if (contentLength > 0 && bodyBytesReceived < contentLength) {
                printf("Error: Connection closed before all data received (%u of %u bytes)\n",
                       bodyBytesReceived, contentLength);
                if (file) fclose(file);
                return HTTP_ERROR_CONNECTION_FAILED;
            }
            break;
        }

        totalReceived += received;
        if (received > 0) {
            idleTimeMs = 0;
        }

        if (!headersParsed) {
            // Still receiving headers
            if (headerBufferUsed + received < sizeof(headerBuffer) - 1) {
                memcpy(headerBuffer + headerBufferUsed, buffer, received);
                headerBufferUsed += received;
                headerBuffer[headerBufferUsed] = '\0';

                // Look for end of headers
                headerEnd = strstr(headerBuffer, "\r\n\r\n");
                if (headerEnd) {
                    headersParsed = 1;
                    headerLength = (headerEnd + 4) - headerBuffer;

                    // Parse status line
                    statusLine = headerBuffer;
                    if (strstr(statusLine, "HTTP/1.1") == statusLine) {
                        strcpy(version, "HTTP/1.1");
                        const char* codeStart = statusLine + 9; // Skip "HTTP/1.1 "
                        unsigned int code = 0;
                        while (*codeStart >= '0' && *codeStart <= '9') {
                            code = code * 10 + (*codeStart - '0');
                            codeStart++;
                        }
                        statusCode = (unsigned short)code;
                    } else if (strstr(statusLine, "HTTP/1.0") == statusLine) {
                        strcpy(version, "HTTP/1.0");
                        const char* codeStart = statusLine + 9; // Skip "HTTP/1.0 "
                        unsigned int code = 0;
                        while (*codeStart >= '0' && *codeStart <= '9') {
                            code = code * 10 + (*codeStart - '0');
                            codeStart++;
                        }
                        statusCode = (unsigned short)code;
                    }

                    printf("%s %s\n", version, HTTP_GetStatusString(statusCode));

                    // Check status code
                    if (statusCode != 200) {
                        if (file) fclose(file);
                        return statusCode;
                    }

                    // Detect Transfer-Encoding: chunked
                    char* transferEncodingStr = strstr(headerBuffer, "Transfer-Encoding:");
                    if (transferEncodingStr) {
                        const char* valueStart = transferEncodingStr + 18; // Skip "Transfer-Encoding:"
                        while (*valueStart == ' ' || *valueStart == '\t') {
                            valueStart++;
                        }

                        char encodingValue[64] = {0};
                        unsigned int encodingIndex = 0;
                        while (*valueStart && *valueStart != '\r' && *valueStart != '\n' &&
                               encodingIndex < (sizeof(encodingValue) - 1U)) {
                            encodingValue[encodingIndex++] = *valueStart;
                            valueStart++;
                        }
                        encodingValue[encodingIndex] = '\0';

                        if (HTTP_StringContainsChunked(encodingValue)) {
                            isChunked = 1;
                            HTTP_ChunkParserInit(&chunkParser);
                            printf("Receiving chunked data");
                        }
                    }

                    // Parse Content-Length
                    if (!isChunked) {
                        char* contentLengthStr = strstr(headerBuffer, "Content-Length:");
                        if (contentLengthStr) {
                            const char* numStart = contentLengthStr + 16; // Skip "Content-Length: "
                            contentLength = 0;
                            while (*numStart >= '0' && *numStart <= '9') {
                                contentLength = contentLength * 10 + (*numStart - '0');
                                numStart++;
                            }
                            printf("Receiving %u bytes", contentLength);
                        }
                    }

                    // Open output file
                    file = fopen(filename, "wb");
                    if (!file) {
                        printf("Error: Could not create file '%s'\n", filename);
                        return HTTP_ERROR_MEMORY_ERROR;
                    }

                    // Process any body data that was received with headers
                    unsigned int bodyDataInBuffer = headerBufferUsed - headerLength;
                    if (bodyDataInBuffer > 0) {
                        unsigned char* bodyStart = (unsigned char*)(headerBuffer + headerLength);
                        if (isChunked) {
                            unsigned int chunkWritten = 0;
                            int chunkResult = HTTP_ChunkParserProcess(&chunkParser, bodyStart, bodyDataInBuffer, file, &chunkWritten);
                            if (chunkResult != HTTP_SUCCESS) {
                                fclose(file);
                                return chunkResult;
                            }
                            bodyBytesReceived += chunkWritten;
                            if (chunkWritten > 0) {
                                printf(".");
                            }
                            if (chunkParser.State == CHUNK_STATE_FINISHED) {
                                break;
                            }
                        } else {
                            if (SaveDataChunkToFile(file, bodyStart, bodyDataInBuffer) != 0) {
                                fclose(file);
                                return HTTP_ERROR_MEMORY_ERROR;
                            }
                            bodyBytesReceived += bodyDataInBuffer;
                        }
                    }
                }
            } else {
                // Header buffer overflow
                printf("Error: HTTP headers too large\n");
                return HTTP_ERROR_INVALID_RESPONSE;
            }
        } else {
            // Headers already parsed, this is body data
            if (isChunked) {
                unsigned int chunkWritten = 0;
                int chunkResult = HTTP_ChunkParserProcess(&chunkParser, (unsigned char*)buffer, (unsigned int)received, file, &chunkWritten);
                if (chunkResult != HTTP_SUCCESS) {
                    fclose(file);
                    return chunkResult;
                }
                bodyBytesReceived += chunkWritten;
                if (chunkWritten > 0) {
                    printf(".");
                }
            } else {
                if (SaveDataChunkToFile(file, (unsigned char*)buffer, received) != 0) {
                    fclose(file);
                    return HTTP_ERROR_MEMORY_ERROR;
                }
                bodyBytesReceived += received;
            }
        }

        // Check if we have received all expected content
        if (headersParsed) {
            if (!isChunked && contentLength > 0 && bodyBytesReceived >= contentLength) {
                break;
            }
            if (isChunked && chunkParser.State == CHUNK_STATE_FINISHED) {
                break;
            }
        }

        // Show progress
        if (headersParsed && !isChunked && contentLength > 0) {
            printf(".");
        }
    }

    if (received == 0 && isChunked && chunkParser.State != CHUNK_STATE_FINISHED) {
        printf("Error: Connection closed before terminating chunk received\n");
        if (file) fclose(file);
        return HTTP_ERROR_CONNECTION_FAILED;
    }

    if (file) {
        fclose(file);
        printf("Finished\n");
    }

    return (statusCode == 200) ? HTTP_SUCCESS : statusCode;
}

/************************************************************************/

/**
 * @brief Print human-readable HTTP error message
 * @param errorCode The HTTP error code to translate
 */
void PrintHttpError(int errorCode) {
    switch (errorCode) {
        case HTTP_ERROR_INVALID_URL:
            printf("Error: Invalid URL format\n");
            break;
        case HTTP_ERROR_CONNECTION_FAILED:
            printf("Error: Connection failed\n");
            break;
        case HTTP_ERROR_TIMEOUT:
            printf("Error: Request timed out\n");
            break;
        case HTTP_ERROR_INVALID_RESPONSE:
            printf("Error: Invalid HTTP response\n");
            break;
        case HTTP_ERROR_MEMORY_ERROR:
            printf("Error: Out of memory\n");
            break;
        case HTTP_ERROR_PROTOCOL_ERROR:
            printf("Error: HTTP protocol error\n");
            break;
        default:
            printf("Error: Unknown error (%d)\n", errorCode);
            break;
    }
}

/************************************************************************/

int exosmain(int argc, char* argv[]) {
    URL parsedUrl;
    HTTP_CONNECTION* connection;
    const char* urlString;
    const char* outputFile;
    int result;

    // Check arguments
    if (argc < 2) {
        PrintUsage();
        return 1;
    }

    urlString = argv[1];

    // Determine output filename
    if (argc >= 3) {
        outputFile = argv[2];
    } else {
        // Parse URL to extract filename
        if (!HTTP_ParseURL(urlString, &parsedUrl)) {
            printf("Error: Could not parse URL '%s'\n", urlString);
            return 1;
        }
        outputFile = ExtractFilename(parsedUrl.Path);
    }

    printf("Downloading: %s to %s\n", urlString, outputFile);

    // Parse the URL
    if (!HTTP_ParseURL(urlString, &parsedUrl)) {
        printf("Error: Invalid URL format\n");
        printf("URL must be in format: http://host[:port]/path\n");
        return 1;
    }

    if (!parsedUrl.Valid) {
        printf("Error: URL validation failed\n");
        return 1;
    }

    printf("Connecting...\n");

    // Create HTTP connection
    connection = HTTP_CreateConnection(parsedUrl.Host, parsedUrl.Port);
    if (!connection) {
        printf("Could not connect to %s:%d\n", parsedUrl.Host, parsedUrl.Port);
        return 1;
    }

    // Send HTTP GET request headers only
    result = HTTP_SendRequest(connection, "GET", parsedUrl.Path, NULL, 0);

    if (result != HTTP_SUCCESS) {
        printf("HTTP request failed: ");
        PrintHttpError(result);
        HTTP_DestroyConnection(connection);
        return 1;
    }

    // Receive response progressively, writing to file and console as we go
    result = HTTP_ReceiveResponseProgressive(connection, outputFile);

    // Cleanup
    HTTP_DestroyConnection(connection);

    if (result == HTTP_SUCCESS) {
        return 0;
    } else {
        printf("\nDownload failed\n");
        return 1;
    }
}
