# Supporting TISM documents

## TISM Message Exchange - Messaging packets and exchanges (Version 1.0)
USE CASE: This document describes a simple data exchange protocol designed to facilitate short messages between microcontrollers ('hosts') that use the TISM programming framework and are connected to the same bus network. The protocol can be implemented as a Stop and Wait ARQ
(Automatic Repeat Request) or (small) sliding window version. It is connectionless, non-routed, aimed at exchange of TISM-messages that contain commands (e.g. controlling actuators) or reading sensor values across different network types (like RS485, Wifi, UART or 433Mhz
radio). The protocol also contains a number of features to discover other hosts and services ('tasks') in the network. Furthermore, separate logical networks across the same medium are supported using a Network ID. In the TISM-framework a payload is maximum 16 bytes; this
version of the protocol supports up to 16 bytes of payload.

Remarks:
- Broadcast messages are not acknowledged by receiving hosts.
- Timeout for responses set to 200 msec for slow connections.
- Retry limit set to 3.
- Bus arbitration (e.g. RS485) is not included.
- Sequence numbers set to 8 bits and intended to wrap around, which should not be an issue in a Stop and Wait ARQ sequence or limited sliding windows.
- When responding to broadcasts, hosts use their own sequence number for UARTMX_SEQ_NUMBER.
- Standard packet sizes range from 12 to 28 bytes. TISM predefined messages have fixed sizes:
-   TISM basic message and acknowledgement packets: 13 bytes
-   TISM connectivity, host and task test packets: 16 bytes
-   TISM host discovery messages: 20 bytes
- Payload size up to 255 (0xFF) bytes can be addressed by UARTMX_MESSAGE_SIZE; in this version of the protocol message sizes > 16 (0x1C) are not supported.
- Maximum packet size is 28 (0x1C) bytes; maximum 16 bytes payload (UARTMX_MESSAGE_SIZE is 0x10), 11 bytes overhead.
- TISM_MESSAGETYPE determines how a packet (and payload) should be processed by the recipient. Custom/user defined message types range from 0x64 (100) to 0xFF (255).
- Each host broadcasts a TISM_INTRODUCE packet every 5 minutes; starting with a random delay between 0-60 seconds before the first broadcast (to prevent collisions).
