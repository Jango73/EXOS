
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
 * @brief Receive HTTP response progressively and write to file
 * @param connection The HTTP connection
 * @param filename The output filename to save the response body
 * @return HTTP_SUCCESS on success, error code otherwise
 */
int HTTP_ReceiveResponseProgressive(HTTP_CONNECTION* connection, const char* filename) {
    HTTP_RESPONSE responseInfo;
    unsigned int bytesWritten = 0;
    int result;

    if (!connection || !filename) {
        return HTTP_ERROR_INVALID_RESPONSE;
    }

    memset(&responseInfo, 0, sizeof(responseInfo));

    result = HTTP_DownloadToFile(connection, filename, &responseInfo, &bytesWritten);

    if (responseInfo.Version[0] != '\0') {
        printf("%s %s\n", responseInfo.Version, HTTP_GetStatusString(responseInfo.StatusCode));
    }

    if (result != HTTP_SUCCESS) {
        return result;
    }

    if (responseInfo.ChunkedEncoding) {
        printf("Receiving chunked data\n");
    } else if (responseInfo.ContentLength > 0U) {
        printf("Receiving %u bytes\n", responseInfo.ContentLength);
    }

    printf("Finished (%u bytes)\n", bytesWritten);

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
