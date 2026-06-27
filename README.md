# tcp-ip-stack-from-scratch

> A user-space OSI layer network protocol stack built entirely from scratch in C — ARP, IPv4, ICMP, TCP, UDP, HTTP, DNS, DHCP, NAT, and ACL — with zero external networking libraries.

**15,000+ lines of C** | **300/300 tests passing** | **Zero OS network API usage**

---

## Overview

`tcp-ip-stack-from-scratch` is a comprehensive network protocol stack simulator that implements core OSI layer protocols without relying on any operating system networking APIs or third-party simulation libraries. Every packet is constructed, routed, and processed manually — from raw Ethernet frames up to application-layer HTTP requests.

The system provides an interactive CLI for designing network topologies, simulating traffic, and inspecting protocol behavior at every layer.

## Architecture (OSI Model)

```
Layer 7 — HTTP · DNS · DHCP · RIP
Layer 4 — TCP (state machine) · UDP
Layer 3 — IPv4 (routing, LPM) · ICMP
Layer 2 — Ethernet · ARP
Layer 1 — Physical link simulation
```

## Key Features

| Layer | Protocol | Implementation Details |
|-------|----------|----------------------|
| **L2** | Ethernet | MAC addressing, frame encapsulation, link simulation |
| **L2** | ARP | Address Resolution Protocol, cache management |
| **L3** | IPv4 | Packet fragmentation/reassembly, TTL, header checksum |
| **L3** | Routing | Longest Prefix Match (LPM) routing table, static routes |
| **L3** | ICMP | Echo (ping), destination unreachable, TTL exceeded |
| **L4** | TCP | Full state machine (SYN, SYN-ACK, ACK, FIN, RST), windowing, sequence numbers |
| **L4** | UDP | Connectionless datagram delivery |
| **L7** | HTTP | Request/response parsing, server simulation |
| **L7** | DNS | Name resolution, record types |
| **L7** | DHCP | Address lease, discovery/offer/request/ack |
| **Infra** | NAT | Network Address Translation |
| **Infra** | ACL | Access Control Lists, packet filtering |
| **GUI** | SDL2 | Optional network topology visualizer |

## Interactive CLI

```
Magi> load topology.json
Magi> ping 192.168.1.2
Magi> traceroute 10.0.0.1
Magi> connect tcp 192.168.1.2:8080
Magi> visualize
Magi> save output.json
```

## Test Suite

The project maintains a comprehensive test suite:
- **300/300 tests passing** across all layers
- Tests cover ARP resolution, IPv4 routing, TCP state transitions, UDP delivery, HTTP parsing, DNS queries, and NAT translation
- Build with `make test` to run

## Build & Run

**Requirements:** GCC (`-Wall -Wextra -std=c11`), Make

```bash
make build
make run
# or: ./run.sh
```

## Project Structure

```
magi_system/
├── core/          # Interface, link, packet, clock simulation
├── layer2/        # Ethernet, ARP
├── layer3/        # IPv4, ICMP, routing
├── layer4/        # TCP, UDP
├── layer7/        # HTTP, DNS, DHCP
├── middleboxes/   # NAT, ACL
├── utils/         # CLI, visualizer, JSON loader
└── gui/           # SDL2 network topology renderer
```

## Why This Matters

Building a network stack from scratch demonstrates:
- Deep understanding of TCP/IP fundamentals (not just API calls)
- Protocol state machine design and debugging
- Systems-level C programming with manual memory management
- Network debugging methodology (packet capture, state inspection)

These skills translate directly to backend infrastructure, distributed systems, and platform engineering roles.
