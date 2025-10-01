
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
typedef struct tag_NETGET_DOWNLOAD_CONTEXT {
    const char* OutputFilename;
    FILE* OutputFile;
    unsigned int ContentLength;
    unsigned int BytesReceived;
    unsigned short StatusCode;
} NETGET_DOWNLOAD_CONTEXT;

/************************************************************************/

static int NetGet_OnHeaders(const HTTP_RESPONSE* response, void* context) {
    NETGET_DOWNLOAD_CONTEXT* downloadContext = (NETGET_DOWNLOAD_CONTEXT*)context;

    if (!downloadContext || !response) {
        return HTTP_ERROR_INVALID_RESPONSE;
    }

    downloadContext->StatusCode = response->StatusCode;
    downloadContext->ContentLength = response->ContentLength;

    printf("%s %s\n", response->Version, HTTP_GetStatusString(response->StatusCode));

    if (response->StatusCode != 200) {
        return (int)response->StatusCode;
    }

    downloadContext->OutputFile = fopen(downloadContext->OutputFilename, "wb");
    if (!downloadContext->OutputFile) {
        printf("Error: Could not create file '%s'\n", downloadContext->OutputFilename);
        return HTTP_ERROR_MEMORY_ERROR;
    }

    if (downloadContext->ContentLength > 0) {
        printf("Receiving %u bytes", downloadContext->ContentLength);
    }

    return HTTP_SUCCESS;
}

/************************************************************************/

static int NetGet_OnBodyData(const unsigned char* data, unsigned int length, void* context) {
    NETGET_DOWNLOAD_CONTEXT* downloadContext = (NETGET_DOWNLOAD_CONTEXT*)context;

    if (!downloadContext || downloadContext->StatusCode != 200) {
        return HTTP_SUCCESS;
    }

    if (!downloadContext->OutputFile) {
        printf("Error: Output file not available for writing\n");
        return HTTP_ERROR_MEMORY_ERROR;
    }

    if (length > 0 && SaveDataChunkToFile(downloadContext->OutputFile, data, length) != 0) {
        return HTTP_ERROR_MEMORY_ERROR;
    }

    downloadContext->BytesReceived += length;

    if (downloadContext->ContentLength > 0) {
        printf(".");
    }

    return HTTP_SUCCESS;
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
    NETGET_DOWNLOAD_CONTEXT downloadContext;
    HTTP_RESPONSE_STREAM_CALLBACKS callbacks;

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

    memset(&downloadContext, 0, sizeof(downloadContext));
    downloadContext.OutputFilename = outputFile;

    callbacks.OnHeaders = NetGet_OnHeaders;
    callbacks.OnBodyData = NetGet_OnBodyData;

    result = HTTP_ReceiveResponseStream(connection, &callbacks, &downloadContext);

    if (downloadContext.OutputFile) {
        fclose(downloadContext.OutputFile);
        downloadContext.OutputFile = NULL;
    }

    // Cleanup
    HTTP_DestroyConnection(connection);

    if (result == HTTP_SUCCESS && downloadContext.StatusCode == 200) {
        printf("Finished\n");
        return 0;
    } else {
        printf("\nDownload failed\n");
        return 1;
    }
}
