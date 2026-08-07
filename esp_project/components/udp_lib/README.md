# UDP Library for ESP32

This library implements a UDP server on ESP32 using the **lwIP** TCP/IP stack. It provides configurable debugging options and supports processing incoming control packets (gamepad, android).

## TODO

- IPv6 support
- Security

## ESP32 Menuconfig
idf.py menuconfig
Component config → lwIP

- Enable SO_RCVBUF option
- Enable IP_PKTINFO option
- Enable LWIP statistics
- UDP: Mailbox size → 32
- TCP/IP task affinity → CPU 0
- Enable lwIP debug

## How to use

`udp_server_init();` to init the UDP server

## Components
- **lwIP** TCP/IP stack
- **esp_timer** for optional timing statistics
- **ledLib** to apply commands

## Configuration Options
- Time stats
- Packet debug
- Info logs
- Address debug

## UDP Server Workflow

1. Launch the UDP task on **core 1** (TCP/IP task affinity is on core 0).
2. Configure the socket:
   - Address family: IPv4
   - Type: UDP (`SOCK_DGRAM`)
   - IP protocol: IPPROTO_IP (auto)
   - Port: defined by `PORT` macro
3. Set socket options:
   - Receive timeout
   - Optional debug options: `SO_RCVBUF`, `IP_PKTINFO`
4. Bind the socket to the address and port.
5. Enter loop:
   - Receive data using `recvfrom`
   - Process the packet depending on its type

## Key Concepts

**Endianness**: Byte order for multi-byte numbers  
- **Little endian**: Low bytes first (ESP32)
- **Big endian**: High bytes first (network / internet, normal reading)

Example: Port 80 = `0x0050`  
- ESP32 memory (little endian): `[0] = 0x50, [1] = 0x00`  
- Network order (big endian): `[0] = 0x00, [1] = 0x50`

**Note:** Always use `htons`/`ntohs` and `htonl`/`ntohl` for network communication.

**UDP (connectionless)**:  
- No handshake or ACK  
- Packets may arrive out of order or be lost
- Lightweight, fast, suitable for frequent messages
