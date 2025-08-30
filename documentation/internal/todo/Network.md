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

## IPv4
- [ ] Header parsing: version, IHL, total length, TTL, protocol, checksum  
- [ ] Defragmentation (optional at first — ignore DF/fragment offset)  
- [ ] Simple routing:  
  - If `dst == myIP` → deliver to local protocol  
  - Else → drop  
- [ ] Encapsulation to Ethernet:  
  - Compute IP checksum  
  - Fill protocol field (`1 = ICMP`, `6 = TCP`, `17 = UDP`)  

## ICMP (recommended before UDP/TCP)
- [ ] Implement Echo Request/Reply (ping)  
- [ ] Validates IPv4 (checksum, send/receive)  

## UDP
- [ ] Simple header: source port, destination port, length, checksum  
  - Can start with `checksum = 0` (disabled)  
- [ ] Basic UDP socket interface  

## TCP (heavy step)
- [ ] State machine  
  - `LISTEN`, `SYN-SENT`, `SYN-RCVD`, `ESTABLISHED`, `FIN-WAIT`, etc.  
- [ ] Sequence and acknowledgment numbers  
- [ ] Sliding windows  
- [ ] Retransmissions  
- [ ] Mini TCP echo server  
  - Accept one connection  
  - Send back everything received  
