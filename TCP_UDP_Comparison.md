# TCP vs. UDP Analysis and Wireshark Traffic Inspection

## 1. Wireshark Packet Analysis Guide

### 1.1 UDP Traffic Analysis (`wireshark/udp_capture.pcapng`)
Capture filter / display filter: `udp.port == 9000`
- **Source IP Address**: IPv4 address of the Vehicle ECU host (e.g., `127.0.0.1` on loopback or local interface IP).
- **Destination IP Address**: IPv4 address of the Dashboard ECU host.
- **Source Port**: Ephemeral UDP port assigned by the OS kernel to `vehicle_ecu` (varies per run/socket).
- **Destination Port**: Fixed listening port of `dashboard_ecu` (e.g., `9000`).
- **Payload Data**: 8-byte UDP header followed by ASCII telemetry string, e.g., `SEQ=0;SPEED=20.00;RPM=1200;STEER=-15.00\n`. No TCP handshake or teardown packets appear.

### 1.2 TCP Traffic Analysis (`wireshark/tcp_capture.pcapng`)
Capture filter / display filter: `tcp.port == 9001`
- **SYN Packet**: Initial segment from Vehicle ECU (`[SYN]`, `Seq=0` relative, `Ack=0`, `Win=...`) requesting connection establishment to port `9001`.
- **SYN-ACK Packet**: Response from Dashboard ECU (`[SYN, ACK]`, `Seq=0` relative, `Ack=1`) acknowledging the SYN and advertising its own initial sequence number.
- **ACK Packet**: Final handshake segment from Vehicle ECU (`[ACK]`, `Seq=1`, `Ack=1`) completing the three-way handshake before application data is sent.
- **Data Transfer Packets**: `[PSH, ACK]` segments carrying ASCII telemetry (`SEQ=...;SPEED=...;RPM=...;STEER=...\n`) at 1-second intervals, paired with `[ACK]` segments from the receiver.
- **Connection Termination Sequence**: Initiated when `vehicle_ecu` closes its socket (`[FIN, ACK]`), acknowledged by `dashboard_ecu` (`[ACK]`), followed by `dashboard_ecu` sending `[FIN, ACK]` and `vehicle_ecu` returning the final `[ACK]` (4-way close / piggybacked FIN-ACK).

## 2. Comparative Study: TCP vs. UDP

| Parameter | TCP (`SOCK_STREAM`) | UDP (`SOCK_DGRAM`) |
| :--- | :--- | :--- |
| **Reliability** | Reliable: sequence numbers, acknowledgements, and retransmissions guarantee delivery of byte stream. | Unreliable (best-effort): no ACKs or retransmissions; datagrams may be lost or arrive out of order. |
| **Speed** | Slightly higher latency due to connection setup, ACKs, flow/congestion control, and stream buffering. | Lower latency and faster transmission because datagrams are sent immediately without handshake or ACKs. |
| **Overhead** | Higher header and protocol overhead (20-byte minimum TCP header plus options, connection state, and ACK traffic). | Low overhead (8-byte fixed UDP header and stateless per-packet operation). |
| **Error Handling** | Header/payload checksum verification plus retransmission of corrupted/lost segments and connection error reporting. | Checksum verification on packet; corrupted or missing datagrams are silently dropped at transport layer with no recovery. |
| **Connection Establishment** | Connection-oriented: requires 3-way handshake (`SYN`, `SYN-ACK`, `ACK`) before data transfer and teardown (`FIN`) on close. | Connectionless: no handshake or persistent connection state; `sendto()` and `recvfrom()` operate directly on addresses. |
| **Typical Usage Scenarios** | Reliable configuration/diagnostic transfers, software/firmware updates, or dashboard streams where loss/reordering is unacceptable. | High-rate or low-latency periodic telemetry/sensor broadcasts where a fresh 1 Hz reading supersedes an older lost sample. |