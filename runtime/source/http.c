
/************************************************************************\

    EXOS Runtime HTTP Client
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


    HTTP Client Implementation

\************************************************************************/

#include "../include/exos.h"
#include "../include/http.h"
#include "../../kernel/include/User.h"

static unsigned int HTTPDefaultReceiveTimeoutMs = 10000; // 10 seconds by default

/***************************************************************************/

void HTTP_SetDefaultReceiveTimeout(unsigned int TimeoutMs) {
    HTTPDefaultReceiveTimeoutMs = TimeoutMs;
    debug("[HTTP_SetDefaultReceiveTimeout] Timeout set to %u ms", HTTPDefaultReceiveTimeoutMs);
}

/***************************************************************************/

unsigned int HTTP_GetDefaultReceiveTimeout(void) {
    return HTTPDefaultReceiveTimeoutMs;
}

/***************************************************************************/

/**
 * @brief Parse a URL string into components
 * @param URLString The URL string to parse
 * @param ParsedURL Output structure to store parsed components
 * @return 1 if parsing was successful, 0 otherwise
 */
int HTTP_ParseURL(const char* URLString, URL* ParsedURL) {
    const char* current;
    const char* schemeEnd;
    const char* hostStart;
    const char* hostEnd;
    const char* pathStart;
    const char* queryStart;
    unsigned int portValue;

    if (!URLString || !ParsedURL) {
        debug("[HTTP_ParseURL] At least one input parameters is NULL");
        return 0;
    }

    // Initialize the structure
    ParsedURL->Scheme[0] = '\0';
    ParsedURL->Host[0] = '\0';
    ParsedURL->Path[0] = '\0';
    ParsedURL->Query[0] = '\0';
    ParsedURL->Port = 0;
    ParsedURL->Valid = 0;

    current = URLString;

    // Find scheme (e.g., "http://")
    schemeEnd = strstr(current, "://");
    if (!schemeEnd) {
        debug("[HTTP_ParseURL] Could not find scheme end");
        return 0;
    }

    // Extract scheme
    if ((unsigned long)(schemeEnd - current) >= (unsigned long)sizeof(ParsedURL->Scheme)) {
        return 0;
    }

    memcpy(ParsedURL->Scheme, current, schemeEnd - current);
    ParsedURL->Scheme[schemeEnd - current] = '\0';

    // Move past "://"
    hostStart = schemeEnd + 3;

    // Find end of host (could be ':', '/', '?', or end of string)
    hostEnd = hostStart;
    while (*hostEnd && *hostEnd != ':' && *hostEnd != '/' && *hostEnd != '?') {
        hostEnd++;
    }

    // Extract host
    if ((unsigned long)(hostEnd - hostStart) >= (unsigned long)sizeof(ParsedURL->Host)) {
        return 0;
    }
    memcpy(ParsedURL->Host, hostStart, hostEnd - hostStart);
    ParsedURL->Host[hostEnd - hostStart] = '\0';

    current = hostEnd;

    // Check for port
    if (*current == ':') {
        current++; // Skip ':'
        portValue = 0;
        while (*current >= '0' && *current <= '9' && *current != '/' && *current != '?') {
            portValue = portValue * 10 + (*current - '0');
            if (portValue > 65535) {
                return 0;
            }
            current++;
        }
        ParsedURL->Port = (unsigned short)portValue;
    }

    // Find path start
    pathStart = current;
    if (*pathStart != '/') {
        // Default path if none specified
        strcpy(ParsedURL->Path, "/");
    } else {
        // Find query string
        queryStart = strstr(pathStart, "?");
        if (queryStart) {
            // Extract path without query
            if ((queryStart - pathStart) >= sizeof(ParsedURL->Path)) {
                return 0;
            }
            memcpy(ParsedURL->Path, pathStart, queryStart - pathStart);
            ParsedURL->Path[queryStart - pathStart] = '\0';

            // Extract query string
            queryStart++; // Skip '?'
            if (strlen(queryStart) < sizeof(ParsedURL->Query)) {
                strcpy(ParsedURL->Query, queryStart);
            }
        } else {
            // No query string, just path
            if (strlen(pathStart) < sizeof(ParsedURL->Path)) {
                strcpy(ParsedURL->Path, pathStart);
            } else {
                return 0;
            }
        }
    }

    // Set default port if not specified
    if (ParsedURL->Port == 0) {
        if (strcmp(ParsedURL->Scheme, "http") == 0) {
            ParsedURL->Port = 80;
        } else {
            return 0; // Only HTTP supported
        }
    }

    // Validate scheme (currently only support HTTP)
    if (strcmp(ParsedURL->Scheme, "http") != 0) {
        return 0;
    }

    ParsedURL->Valid = 1;
    return 1;
}

/***************************************************************************/

/**
 * @brief Create HTTP connection to host
 * @param Host The hostname or IP address
 * @param Port The port number (usually 80 for HTTP)
 * @return Pointer to HTTP_CONNECTION or NULL on failure
 */
HTTP_CONNECTION* HTTP_CreateConnection(const char* Host, unsigned short Port) {
    HTTP_CONNECTION* connection;
    struct sockaddr_in serverAddr;
    int result;

    debug("[HTTP_CreateConnection] Host=%s, Port=%d", Host, Port);

    if (!Host || Port == 0) {
        debug("[HTTP_CreateConnection] Invalid parameters");
        return NULL;
    }

    // Allocate connection structure
    connection = (HTTP_CONNECTION*)malloc(sizeof(HTTP_CONNECTION));
    if (!connection) {
        return NULL;
    }

    debug("[HTTP_CreateConnection] connection = %x", connection);

    // Initialize connection
    memset(connection, 0, sizeof(HTTP_CONNECTION));
    connection->RemotePort = Port;
    connection->Connected = 0;
    connection->KeepAlive = 0;

    // Create socket
    connection->SocketHandle = socket(SOCKET_AF_INET, SOCKET_TYPE_STREAM, SOCKET_PROTOCOL_TCP);
    if (connection->SocketHandle == 0) {
        free(connection);
        return NULL;
    }

    // Parse IP address if it's in dotted decimal notation
    connection->RemoteIP = InternetAddressFromString((LPCSTR)Host);

    // Simple hardcoded test for "52.204.95.73"
    /*
    if (strcmp(Host, "52.204.95.73") == 0) {
        connection->RemoteIP = (52 << 24) | (204 << 16) | (95 << 8) | 73;
    } else if (strcmp(Host, "192.168.56.1") == 0) {
        connection->RemoteIP = (192 << 24) | (168 << 16) | (56 << 8) | 1;
    } else if (strcmp(Host, "10.0.2.2") == 0) {
        connection->RemoteIP = (10 << 24) | (0 << 16) | (2 << 8) | 2;
    } else {
        // For now, only support these specific IPs
        connection->RemoteIP = 0;
    }
    */

    if (connection->RemoteIP == 0) {
        debug("[HTTP_CreateConnection] Failed to parse IP address");
        // For now, we don't support hostname resolution
        free(connection);
        return NULL;
    }

    debug("[HTTP_CreateConnection] IP parsed successfully, RemoteIP=%x, RemotePort=%d", connection->RemoteIP, connection->RemotePort);

    // Setup server address
    memset(&serverAddr, 0, sizeof(serverAddr));
    serverAddr.sin_family = SOCKET_AF_INET;
    serverAddr.sin_port = htons(Port);
    serverAddr.sin_addr = htonl(connection->RemoteIP);

    // Apply configured receive timeout (0 disables the timeout)
    unsigned int timeoutMs = HTTPDefaultReceiveTimeoutMs;
    if (timeoutMs > 0) {
        if (setsockopt(connection->SocketHandle, SOL_SOCKET, SO_RCVTIMEO, &timeoutMs, sizeof(timeoutMs)) != 0) {
            debug("[HTTP_CreateConnection] Failed to set receive timeout");
        } else {
            debug("[HTTP_CreateConnection] Receive timeout set to %u ms", timeoutMs);
        }
    } else {
        debug("[HTTP_CreateConnection] Receive timeout disabled");
    }

    // Connect to server
    result = connect(connection->SocketHandle, (struct sockaddr*)&serverAddr, sizeof(serverAddr));

    if (result != 0) {
        debug("[HTTP_CreateConnection] connect failed");
        shutdown(connection->SocketHandle, SOCKET_SHUTDOWN_BOTH);
        free(connection);
        return NULL;
    }

    debug("[HTTP_CreateConnection] connect initiated, waiting for establishment...");

    // Wait for connection to be established using adaptive delay
    // In EXOS, connect() is non-blocking, so we need to wait for TCP handshake
    AdaptiveDelay_Initialize(&(connection->DelayState));

    // Override defaults for HTTP connection timeout
    connection->DelayState.MinDelay = 50;      // 50 ticks minimum
    connection->DelayState.MaxDelay = 2000;    // 2000 ticks maximum
    connection->DelayState.MaxAttempts = 10;   // 10 attempts max

    while (AdaptiveDelay_ShouldContinue(&(connection->DelayState))) {
        struct sockaddr_in peerAddr;
        socklen_t peerAddrLen = sizeof(peerAddr);

        // Try to get peer address - this succeeds only if connection is established
        if (getpeername(connection->SocketHandle, (struct sockaddr*)&peerAddr, &peerAddrLen) == 0) {
            debug("[HTTP_CreateConnection] connection established after %d attempts", connection->DelayState.AttemptCount);
            connection->Connected = 1;
            AdaptiveDelay_OnSuccess(&(connection->DelayState));
            return connection;
        }

        // Get next delay and sleep
        U32 delayTicks = AdaptiveDelay_GetNextDelay(&connection->DelayState);
        if (delayTicks > 0) {
            debug("[HTTP_CreateConnection] attempt %d failed, waiting %d ticks", connection->DelayState.AttemptCount, delayTicks);
            sleep(delayTicks);
            AdaptiveDelay_OnFailure(&(connection->DelayState));
        }
    }

    debug("[HTTP_CreateConnection] connection timeout after %d attempts", connection->DelayState.AttemptCount);
    shutdown(connection->SocketHandle, SOCKET_SHUTDOWN_BOTH);
    free(connection);
    return NULL;
}

/***************************************************************************/

/**
 * @brief Destroy HTTP connection
 * @param Connection The connection to destroy
 */
void HTTP_DestroyConnection(HTTP_CONNECTION* Connection) {
    if (!Connection) {
        return;
    }

    if (Connection->Connected && Connection->SocketHandle != 0) {
        shutdown(Connection->SocketHandle, SOCKET_SHUTDOWN_BOTH);
    }

    if (Connection->CurrentRequest) {
        if (Connection->CurrentRequest->Body) {
            free(Connection->CurrentRequest->Body);
        }
        free(Connection->CurrentRequest);
    }

    if (Connection->CurrentResponse) {
        HTTP_FreeResponse(Connection->CurrentResponse);
        free(Connection->CurrentResponse);
    }

    free(Connection);
}

/***************************************************************************/

int HTTP_SendRequest(HTTP_CONNECTION* Connection, const char* Method, const char* Path,
                           const unsigned char* Body, unsigned int BodyLength) {
    char request[2048];
    int requestLen;
    int sent;

    debug("[HTTP_SendRequest] Method=%s, Path=%s", Method, Path);
    debug("[HTTP_SendRequest] Connection=%x, RemoteIP=%x, RemotePort=%d", (unsigned int)Connection, Connection->RemoteIP, Connection->RemotePort);

    if (!Connection || !Connection->Connected || !Method || !Path) {
        debug("[HTTP_SendRequest] Invalid parameters");
        return HTTP_ERROR_INVALID_URL;
    }

    // Build HTTP request
    debug("[HTTP_SendRequest] RemoteIP=%x, RemotePort=%d", Connection->RemoteIP, Connection->RemotePort);

    if (Body && BodyLength > 0) {
        requestLen = sprintf(request,
            "%s %s HTTP/1.1\r\n"
            "Host: %d.%d.%d.%d:%d\r\n"
            "User-Agent: EXOS/1.0\r\n"
            "Connection: close\r\n"
            "Content-Length: %u\r\n"
            "\r\n",
            Method, Path,
            (Connection->RemoteIP >> 24) & 0xFF,
            (Connection->RemoteIP >> 16) & 0xFF,
            (Connection->RemoteIP >> 8) & 0xFF,
            Connection->RemoteIP & 0xFF,
            Connection->RemotePort,
            BodyLength);
    } else {
        requestLen = sprintf(request,
            "%s %s HTTP/1.1\r\n"
            "Host: %d.%d.%d.%d:%d\r\n"
            "User-Agent: EXOS/1.0\r\n"
            "Connection: close\r\n"
            "\r\n",
            Method, Path,
            (Connection->RemoteIP >> 24) & 0xFF,
            (Connection->RemoteIP >> 16) & 0xFF,
            (Connection->RemoteIP >> 8) & 0xFF,
            Connection->RemoteIP & 0xFF,
            Connection->RemotePort);
    }

    // Send request headers
    debug("[HTTP_SendRequest] Sending %d", requestLen, requestLen > 100 ? 100 : requestLen);
    sent = send(Connection->SocketHandle, request, requestLen, 0);
    if (sent != requestLen) {
        debug("[HTTP_SendRequest] Send failed: sent=%d, expected=%d", sent, requestLen);
        return HTTP_ERROR_CONNECTION_FAILED;
    }
    debug("[HTTP_SendRequest] Headers sent successfully");

    // Send body if present
    if (Body && BodyLength > 0) {
        sent = send(Connection->SocketHandle, Body, BodyLength, 0);
        if (sent != (int)BodyLength) {
            return HTTP_ERROR_CONNECTION_FAILED;
        }
    }

    return HTTP_SUCCESS;
}

/***************************************************************************/

int HTTP_ReceiveResponse(HTTP_CONNECTION* Connection, HTTP_RESPONSE* Response) {
    char buffer[1024];
    int received;
    int totalReceived = 0;
    char* headerEnd;
    char* statusLine;
    int retryCount = 0;
    int timeoutCount = 0;
    const int maxRetries = 50; // Allow up to 50 attempts with small delays
    const int maxTimeoutsBeforeStateCheck = 3;

    debug("[HTTP_ReceiveResponse] Starting to receive response");
    char* contentLengthStr;
    unsigned int contentLength = 0;
    int headersParsed = 0;
    unsigned int savedHeaderLength = 0;

    // Dynamic buffer to accumulate all response data
    unsigned char* allData = NULL;
    unsigned int allDataSize = 0;
    unsigned int allDataCapacity = 4096;

    if (!Connection || !Response) {
        debug("[HTTP_ReceiveResponse] Invalid parameters");
        return HTTP_ERROR_INVALID_RESPONSE;
    }

    // Initialize response
    memset(Response, 0, sizeof(HTTP_RESPONSE));
    Response->StatusCode = 0;
    Response->ContentLength = 0;
    Response->ChunkedEncoding = 0;

    // Allocate initial buffer for all data
    allData = (unsigned char*)malloc(allDataCapacity);
    if (!allData) {
        debug("[HTTP_ReceiveResponse] Failed to allocate buffer");
        return HTTP_ERROR_MEMORY_ERROR;
    }

    // Reset receive buffer
    Connection->ReceiveBufferUsed = 0;

    // Receive response with retry logic
    debug("[HTTP_ReceiveResponse] Starting receive loop");

    while (1) {  // Continue until we have complete response
        received = recv(Connection->SocketHandle, buffer, sizeof(buffer), 0);
        debug("[HTTP_ReceiveResponse] recv() returned %d bytes", received);

        if (received >= 0) {
            if (received == 0) {
                debug("[HTTP_ReceiveResponse] recv() returned 0 - connection closed by server after %d bytes", totalReceived);
                break;
            }

            // Reset retry counters on successful receive
            retryCount = 0;
            timeoutCount = 0;

        } else if (received == SOCKET_ERROR_WOULDBLOCK) {
            retryCount++;
            if (retryCount >= maxRetries) {
                debug("[HTTP_ReceiveResponse] recv() would block after %d retries", retryCount);
                break;
            }

            debug("[HTTP_ReceiveResponse] recv() would block, retry %d/%d", retryCount, maxRetries);
            sleep(1);
            continue;

        } else if (received == SOCKET_ERROR_TIMEOUT) {
            retryCount++;
            timeoutCount++;
            debug("[HTTP_ReceiveResponse] recv() timeout %d (retry %d/%d)", timeoutCount, retryCount, maxRetries);

            if (timeoutCount >= maxTimeoutsBeforeStateCheck) {
                struct sockaddr_in peerAddr;
                socklen_t peerAddrLen = sizeof(peerAddr);
                int peerStatus = getpeername(Connection->SocketHandle, (struct sockaddr*)&peerAddr, &peerAddrLen);

                if (peerStatus == 0) {
                    debug("[HTTP_ReceiveResponse] Connection alive after %d timeouts, continuing", timeoutCount);
                    timeoutCount = 0;
                } else if (peerStatus == SOCKET_ERROR_NOTCONNECTED) {
                    debug("[HTTP_ReceiveResponse] Connection lost while waiting for data");
                    received = 0;
                    break;
                } else {
                    debug("[HTTP_ReceiveResponse] Failed to verify connection state (%d)", peerStatus);
                    break;
                }
            }

            if (retryCount >= maxRetries) {
                debug("[HTTP_ReceiveResponse] Maximum retries reached after timeout");
                break;
            }

            sleep(1);
            continue;

        } else {
            debug("[HTTP_ReceiveResponse] recv() error: %d", received);
            break;
        }

        // Expand allData buffer if needed
        if (allDataSize + received > allDataCapacity) {
            allDataCapacity = allDataCapacity * 2;
            if (allDataSize + received > allDataCapacity) {
                allDataCapacity = allDataSize + received + 1024;
            }
            debug("[HTTP_ReceiveResponse] Expanding buffer to %d bytes", allDataCapacity);
            unsigned char* newBuffer = (unsigned char*)malloc(allDataCapacity);
            if (!newBuffer) {
                debug("[HTTP_ReceiveResponse] Failed to expand buffer");
                free(allData);
                return HTTP_ERROR_MEMORY_ERROR;
            }
            memcpy(newBuffer, allData, allDataSize);
            free(allData);
            allData = newBuffer;
        }

        // Copy received data to allData buffer
        memcpy(allData + allDataSize, buffer, received);
        allDataSize += received;
        totalReceived += received;

        // Copy to receive buffer (for header parsing only) up to buffer size
        if (Connection->ReceiveBufferUsed + received < sizeof(Connection->ReceiveBuffer)) {
            memcpy(Connection->ReceiveBuffer + Connection->ReceiveBufferUsed, buffer, received);
            Connection->ReceiveBufferUsed += received;
        }

        // Look for end of headers
        if (!headersParsed) {
            allData[allDataSize] = '\0';
            headerEnd = strstr((char*)allData, "\r\n\r\n");
            if (headerEnd) {
                headersParsed = 1;
                debug("[HTTP_ReceiveResponse] Headers parsed, received %d bytes total", totalReceived);

                // Parse Content-Length from headers and save header length
                savedHeaderLength = (headerEnd + 4) - (char*)allData;
                char tempHeaders[4096];
                if (savedHeaderLength < sizeof(tempHeaders)) {
                    memcpy(tempHeaders, allData, savedHeaderLength);
                    tempHeaders[savedHeaderLength] = '\0';

                    contentLengthStr = strstr(tempHeaders, "Content-Length:");
                    if (contentLengthStr) {
                        const char* numStart = contentLengthStr + 16; // Skip "Content-Length: "
                        contentLength = 0;
                        while (*numStart >= '0' && *numStart <= '9') {
                            contentLength = contentLength * 10 + (*numStart - '0');
                            numStart++;
                        }
                        debug("[HTTP_ReceiveResponse] Content-Length: %d, headerLength: %d", contentLength, savedHeaderLength);
                    }
                }
            }
        }

        // Show progress for debugging but don't stop on Content-Length (servers can lie)
        if (headersParsed && contentLength > 0 && savedHeaderLength > 0) {
            unsigned int currentBody = allDataSize - savedHeaderLength;
            debug("[HTTP_ReceiveResponse] Progress: headers: %d, body: %d (server claims: %d)",
                  savedHeaderLength, currentBody, contentLength);
        }
    }

    if (totalReceived == 0) {
        debug("[HTTP_ReceiveResponse] No data received after %d retries", retryCount);
        free(allData);
        return HTTP_ERROR_CONNECTION_FAILED;
    }

    debug("[HTTP_ReceiveResponse] Total received: %d bytes, allDataSize: %d bytes", totalReceived, allDataSize);

    // Null-terminate allData for string operations
    allData[allDataSize] = '\0';

    // Parse status line
    statusLine = (char*)allData;
    if (strstr(statusLine, "HTTP/1.1") == statusLine) {
        strcpy(Response->Version, "HTTP/1.1");
        // Parse status code
        const char* codeStart = statusLine + 9; // Skip "HTTP/1.1 "
        unsigned int code = 0;
        while (*codeStart >= '0' && *codeStart <= '9') {
            code = code * 10 + (*codeStart - '0');
            codeStart++;
        }
        if (code <= 65535) {
            Response->StatusCode = (unsigned short)code;
        } else {
            free(allData);
            return HTTP_ERROR_INVALID_RESPONSE;
        }
    } else if (strstr(statusLine, "HTTP/1.0") == statusLine) {
        strcpy(Response->Version, "HTTP/1.0");
        // Parse status code
        const char* codeStart = statusLine + 9; // Skip "HTTP/1.0 "
        unsigned int code = 0;
        while (*codeStart >= '0' && *codeStart <= '9') {
            code = code * 10 + (*codeStart - '0');
            codeStart++;
        }
        if (code <= 65535) {
            Response->StatusCode = (unsigned short)code;
        } else {
            free(allData);
            return HTTP_ERROR_INVALID_RESPONSE;
        }
    } else {
        free(allData);
        return HTTP_ERROR_INVALID_RESPONSE;
    }

    // Find headers end in allData
    headerEnd = strstr((char*)allData, "\r\n\r\n");
    if (!headerEnd) {
        free(allData);
        return HTTP_ERROR_INVALID_RESPONSE;
    }

    // Copy headers
    unsigned int headerLength = (headerEnd + 4) - (char*)allData;
    if (headerLength < sizeof(Response->Headers)) {
        memcpy(Response->Headers, allData, headerLength);
        Response->Headers[headerLength] = '\0';
    }

    // Check for Content-Length
    contentLengthStr = strstr(Response->Headers, "Content-Length:");
    if (contentLengthStr) {
        const char* numStart = contentLengthStr + 16; // Skip "Content-Length: "
        contentLength = 0;
        while (*numStart >= '0' && *numStart <= '9') {
            contentLength = contentLength * 10 + (*numStart - '0');
            numStart++;
        }
        Response->ContentLength = contentLength;
    }

    // Check for chunked encoding
    if (strstr(Response->Headers, "Transfer-Encoding: chunked")) {
        Response->ChunkedEncoding = 1;
    }

    // Extract body from allData
    unsigned char* bodyStart = (unsigned char*)(headerEnd + 4);
    unsigned int bodyLength = allDataSize - (bodyStart - allData);

    debug("[HTTP_ReceiveResponse] Header ends at offset %d, body starts at offset %d, body length: %d",
          (int)(headerEnd + 4 - (char*)allData), (int)(bodyStart - allData), bodyLength);

    if (bodyLength > 0) {
        Response->Body = (unsigned char*)malloc(bodyLength + 1);
        if (Response->Body) {
            memcpy(Response->Body, bodyStart, bodyLength);
            Response->Body[bodyLength] = '\0';
            Response->BodyLength = bodyLength;
            debug("[HTTP_ReceiveResponse] Successfully copied %d bytes to Response->Body", bodyLength);
        } else {
            debug("[HTTP_ReceiveResponse] Failed to allocate %d bytes for Response->Body", bodyLength + 1);
        }
    } else {
        debug("[HTTP_ReceiveResponse] No body data to extract (bodyLength = %d)", bodyLength);
    }

    free(allData);
    return HTTP_SUCCESS;
}

/***************************************************************************/

/**
 * @brief Send HTTP GET request
 * @param Connection The HTTP connection
 * @param Path The request path
 * @param Response The response structure to fill
 * @return HTTP_SUCCESS on success, error code otherwise
 */
int HTTP_Get(HTTP_CONNECTION* Connection, const char* Path, HTTP_RESPONSE* Response) {
    int result;

    debug("[HTTP_Get] Sending GET request for path: %s", Path);
    result = HTTP_SendRequest(Connection, "GET", Path, NULL, 0);
    if (result != HTTP_SUCCESS) {
        debug("[HTTP_Get] HTTP_SendRequest failed with result: %d", result);
        return result;
    }

    debug("[HTTP_Get] Request sent successfully, receiving response");
    result = HTTP_ReceiveResponse(Connection, Response);
    debug("[HTTP_Get] HTTP_ReceiveResponse returned: %d", result);
    return result;
}

/***************************************************************************/

/**
 * @brief Send HTTP POST request
 * @param Connection The HTTP connection
 * @param Path The request path
 * @param Body The request body data
 * @param BodyLength The length of the request body
 * @param Response The response structure to fill
 * @return HTTP_SUCCESS on success, error code otherwise
 */
int HTTP_Post(HTTP_CONNECTION* Connection, const char* Path, const unsigned char* Body,
              unsigned int BodyLength, HTTP_RESPONSE* Response) {
    int result;

    result = HTTP_SendRequest(Connection, "POST", Path, Body, BodyLength);
    if (result != HTTP_SUCCESS) {
        return result;
    }

    return HTTP_ReceiveResponse(Connection, Response);
}

/***************************************************************************/

/**
 * @brief Free response data
 * @param Response The response to free
 */
void HTTP_FreeResponse(HTTP_RESPONSE* Response) {
    if (!Response) {
        return;
    }

    if (Response->Body) {
        free(Response->Body);
        Response->Body = NULL;
    }

    Response->BodyLength = 0;
    Response->ContentLength = 0;
}

/***************************************************************************/

/**
 * @brief Get header value from response
 * @param Response The HTTP response
 * @param HeaderName The header name to search for
 * @return Pointer to header value or NULL if not found
 */
const char* HTTP_GetHeader(const HTTP_RESPONSE* Response, const char* HeaderName) {
    char* headerPos;
    char* valueStart;
    char* valueEnd;
    static char headerValue[256];

    if (!Response || !HeaderName || Response->Headers[0] == 0) {
        return NULL;
    }

    headerPos = strstr(Response->Headers, HeaderName);
    if (!headerPos) {
        return NULL;
    }

    // Find the colon
    valueStart = strstr(headerPos, ":");
    if (!valueStart) {
        return NULL;
    }

    // Skip colon and spaces
    valueStart++;
    while (*valueStart == ' ' || *valueStart == '\t') {
        valueStart++;
    }

    // Find end of line
    valueEnd = strstr(valueStart, "\r\n");
    if (!valueEnd) {
        valueEnd = strstr(valueStart, "\n");
    }

    if (!valueEnd) {
        return NULL;
    }

    // Copy value
    unsigned int valueLength = valueEnd - valueStart;
    if (valueLength >= sizeof(headerValue)) {
        valueLength = sizeof(headerValue) - 1;
    }

    memcpy(headerValue, valueStart, valueLength);
    headerValue[valueLength] = '\0';

    return headerValue;
}

/***************************************************************************/

/**
 * @brief Get HTTP status description string
 * @param StatusCode HTTP status code
 * @return Pointer to status description string
 */
const char* HTTP_GetStatusString(unsigned short StatusCode) {
    switch (StatusCode) {
        // 1xx Informational
        case 100: return "100 - Continue";
        case 101: return "101 - Switching Protocols";

        // 2xx Success
        case 200: return "200 - OK";
        case 201: return "201 - Created";
        case 202: return "202 - Accepted";
        case 204: return "204 - No Content";
        case 206: return "206 - Partial Content";

        // 3xx Redirection
        case 300: return "300 - Multiple Choices";
        case 301: return "301 - Moved Permanently";
        case 302: return "302 - Found";
        case 304: return "304 - Not Modified";
        case 307: return "307 - Temporary Redirect";
        case 308: return "308 - Permanent Redirect";

        // 4xx Client Error
        case 400: return "400 - Bad Request";
        case 401: return "401 - Unauthorized";
        case 403: return "403 - Forbidden";
        case 404: return "404 - Not Found";
        case 405: return "405 - Method Not Allowed";
        case 406: return "406 - Not Acceptable";
        case 408: return "408 - Request Timeout";
        case 409: return "409 - Conflict";
        case 410: return "410 - Gone";
        case 411: return "411 - Length Required";
        case 413: return "413 - Payload Too Large";
        case 414: return "414 - URI Too Long";
        case 415: return "415 - Unsupported Media Type";
        case 416: return "416 - Range Not Satisfiable";
        case 418: return "418 - I'm a teapot";
        case 429: return "429 - Too Many Requests";

        // 5xx Server Error
        case 500: return "500 - Internal Server Error";
        case 501: return "501 - Not Implemented";
        case 502: return "502 - Bad Gateway";
        case 503: return "503 - Service Unavailable";
        case 504: return "504 - Gateway Timeout";
        case 505: return "505 - HTTP Version Not Supported";

        default: return "Unknown Status Code";
    }
}
