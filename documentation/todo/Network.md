# Networking Stack Roadmap

## Ethernet (E1000 driver)
- [X] TX/RX descriptors, frame reception  
- [X] Recognize Ethertype  
  - `0x0800 = IPv4`  
  - `0x0806 = ARP`  

## ARP
- [X] IPv4 address → MAC address resolution
- [X] ARP cache with expiration
- [X] Reply to incoming ARP requests for local IP

## DHCP
- [ ] DHCP client implementation
  - DISCOVER, OFFER, REQUEST, ACK sequence
  - Automatic IP address acquisition
  - Subnet mask, gateway, DNS server configuration
- [ ] Lease management
  - Lease expiration tracking
  - Automatic renewal (T1/T2 timers)
  - DHCP RELEASE on shutdown
- [ ] DHCP options parsing
  - Option 1: Subnet mask
  - Option 3: Router (default gateway)
  - Option 6: DNS servers
  - Option 51: IP address lease time
  - Option 53: DHCP message type
  - Option 54: DHCP server identifier
- [ ] Basic error handling
  - Retransmission on timeout
  - Fallback to static IP if DHCP fails
  - Handle DHCP NAK responses

## IPv4
- [X] Header parsing: version, IHL, total length, TTL, protocol, checksum
- [ ] Defragmentation (optional at first — ignore DF/fragment offset)
- [X] Simple routing:
  - If `dst == myIP` → deliver to local protocol
  - Else → drop
- [X] Encapsulation to Ethernet:
  - Compute IP checksum
  - Fill protocol field (`1 = ICMP`, `6 = TCP`, `17 = UDP`)  

## ICMP (recommended before UDP/TCP)
- [ ] Implement Echo Request/Reply (ping)  
- [ ] Validates IPv4 (checksum, send/receive)  

## UDP
- [ ] Simple header: source port, destination port, length, checksum  
  - Can start with `checksum = 0` (disabled)  
- [ ] Basic UDP socket interface  

## TCP
- [X] State machine using StateMachine.c framework
  - `CLOSED`, `LISTEN`, `SYN-SENT`, `SYN-RECEIVED`, `ESTABLISHED`, `FIN-WAIT-1`, `FIN-WAIT-2`, `CLOSE-WAIT`, `CLOSING`, `LAST-ACK`, `TIME-WAIT`
  - 25+ state transitions according to RFC 793
  - Event-driven architecture with TCP_EVENT_* events
- [X] Basic sequence and acknowledgment numbers
  - Initial sequence number generation
  - ACK validation in state transitions
- [X] TCP header structure and checksum calculation
  - Complete TCPHeader structure with all fields
  - TCP pseudo-header checksum validation
- [X] Connection management
  - TCP_CreateConnection, TCP_Connect, TCP_Listen, TCP_Close
  - Support for up to 16 simultaneous connections
  - Connection identification by (local IP:port, remote IP:port)
- [X] Basic data transmission
  - TCP_Send and TCP_Receive functions
  - Send/receive buffers (1KB each)
  - PSH+ACK flag support for immediate data transmission
- [X] Timer management
  - TIME-WAIT timeout (30 seconds)
  - Basic retransmission timer structure
  - TCP_Update() for periodic timer processing
- [X] IPv4 integration
  - Registered as IPv4 protocol handler (protocol 6)
  - Automatic packet reception and state machine processing
- [X] Basic testing framework
  - Integration in Test-Network.c
  - Connection creation and state transition verification

### TCP Advanced Features
- [X] Sliding windows implementation
  - Window size negotiation
  - Flow control mechanisms
  - Receive window management
- [ ] Advanced retransmissions
  - Exponential backoff
  - Fast retransmit and fast recovery
  - Congestion control (Tahoe/Reno algorithms)
- [ ] Performance optimizations
  - Nagle algorithm implementation
  - Delayed ACK support
  - Keep-alive mechanism
- [ ] Advanced connection handling
  - Simultaneous open support
  - Half-close connections
  - Connection reset handling
- [X] Socket interface
  - Berkeley socket API compatibility
  - Non-blocking I/O support
  - Socket options (SO_REUSEADDR, etc.)
- [ ] Mini TCP echo server
  - Accept multiple connections
  - Send back everything received
  - Demonstrate full TCP functionality

## HTTP Client Implementation
- [X] HTTP client framework
  - TCP connection management for HTTP requests
  - Request building and transmission
  - Response reception and parsing
  - Connection pooling and reuse
- [X] HTTP request generation
  - GET request formatting with proper headers
  - Host header generation from URL
  - User-Agent and other standard headers
  - Request validation and error handling
- [X] HTTP response processing
  - Status line parsing (HTTP version, status code, reason phrase)
  - Response header extraction and validation
  - Content-Length and Transfer-Encoding handling
  - Body data reception and buffering
- [X] Error handling and retries
  - Network timeout handling
  - HTTP error status code processing
  - Connection failure recovery
  - Basic retry logic for transient failures

## HTTP Protocol Implementation
- [ ] HTTP protocol support
  - Basic HTTP/1.1 request/response handling
  - GET, POST, HEAD method support
  - Request header parsing (Host, Content-Length, etc.)
  - Response header generation (Content-Type, Content-Length, etc.)
  - Status code handling (200, 404, 500, etc.)
- [ ] HTTP message parsing
  - Request line parsing (method, URI, version)
  - Header field parsing and validation
  - Chunked transfer encoding support
  - Content-Length based body handling
- [ ] HTTP server framework
  - Basic HTTP server implementation
  - Request routing and handler registration
  - Static file serving capabilities
  - Keep-alive connection support

## HTTP Download Application (system/netget)
- [X] Command-line HTTP client application
  - URL parsing and validation (protocol, host, port, path)
  - Integration with kernel HTTP client functionality
  - File download capabilities from HTTP URLs
  - Progress reporting during downloads
- [X] File management
  - Automatic filename extraction from URL path
  - Download destination directory handling
  - File overwrite protection and confirmation
  - Temporary file handling during download
- [X] User interface
  - Command-line argument parsing (URL, output file, options)
  - Download progress display (bytes transferred, percentage)
  - Error reporting and user feedback
  - Verbose mode for debugging network operations
- [X] Integration features
  - Use kernel's HTTP client stack for network operations
  - Leverage existing TCP/IP implementation
  - Support for HTTP redirects and error responses
  - Resume capability for interrupted downloads
