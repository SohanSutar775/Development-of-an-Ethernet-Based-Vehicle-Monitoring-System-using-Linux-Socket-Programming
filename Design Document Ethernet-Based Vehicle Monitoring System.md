# Design Document: Ethernet-Based Vehicle Monitoring System

## 1. System Architecture
Both ECU applications execute as separate Linux user-space processes in distinct terminals on a single Ubuntu host, communicating via the Linux IP/Ethernet networking stack (loopback `lo` or a configured Ethernet interface).

```text
Vehicle ECU (Terminal 2)          Linux IP/Ethernet Stack          Dashboard ECU (Terminal 1)
+-----------------------+       +-------------------------+       +-------------------------+
| Telemetry Generator   |       | Socket Layer (AF_INET)  |       | Socket Receiver         |
| - Vehicle Speed       | ----> | - UDP: SOCK_DGRAM       | ----> | - Frame / Datagram Read |
| - Engine RPM          |       | - TCP: SOCK_STREAM      |       | - Telemetry Parser      |
| - Steering Angle      |       |                         |       | - Console Display       |
+-----------------------+       +-------------------------+       +-------------------------+