# DineSync

A multi-location restaurant chain management system built in C++17 using raw TCP sockets and a length-prefixed JSON protocol.

## Overview

DineSync extends a single-restaurant server into a distributed chain platform. A **Central Hub Server** coordinates two or more **Location Servers**. Each location runs its own concurrent multi-threaded TCP server serving local clients (Manager, Server, Chef), while also maintaining an upstream connection to the Hub for menu synchronization, order aggregation, and health monitoring.

## Architecture

```
Location B Clients (Manager / Server / Chef)
        │ TCP :5471
        ▼
 Location Server B :5471
   ├── Local Menu
   └── Orders
        │  REGISTER / HEARTBEAT / REPORT_ORDERS / MENU_UPDATE_PUSH
        ▼
 Central Hub Server :6000          ◄── Chain Admin (TCP :6000)
   ├── Master Menu
   ├── Location Registry
   └── Order Aggregator
        │  REGISTER / HEARTBEAT / REPORT_ORDERS / MENU_UPDATE_PUSH
        ▼
 Location Server A :5470
   ├── Local Menu
   └── Orders
        ▲
        │ TCP :5470
Location A Clients (Manager / Server / Chef)
```

**Two-tier design:**
- **Tier 1** — Location Clients ↔ Location Server (same as Project 1)
- **Tier 2** — Location Server ↔ Central Hub (new in Project 2)

## Features

| Requirement | Status |
|---|---|
| Central Hub with location registry | ✅ |
| Menu sync — Hub pushes updates to all locations | ✅ |
| Concurrent multi-threaded Location Server | ✅ |
| Chain Admin role (manage menu, query aggregate data) | ✅ |
| Order aggregation — chain-wide revenue & order counts | ✅ |
| Heartbeat — Hub pings locations every 5 s, marks offline on failure | ✅ |
| Graceful degradation — locations keep running on Hub disconnect | ✅ |

## Protocol

Every message uses **4-byte big-endian length prefix + JSON payload** (same framing as Project 1).

### Location Server ↔ Hub

| Message | Direction | Purpose |
|---|---|---|
| `REGISTER_LOCATION_REQ/RESP` | Loc → Hub | Register on startup; Hub returns master menu |
| `HEARTBEAT_REQ/RESP` | Hub → Loc | Liveness check every 5 s |
| `MENU_UPDATE_PUSH` | Hub → Loc | Propagate price/add/remove changes |
| `REPORT_ORDERS_REQ/RESP` | Loc → Hub | Report completed orders for aggregation |

### Chain Admin ↔ Hub

| Message | Purpose |
|---|---|
| `ADMIN_LOGIN_REQ/RESP` | Authenticate as `chain_admin` |
| `ADMIN_UPDATE_MENU_REQ/RESP` | Update master menu price (triggers push to all locations) |
| `AGGREGATE_QUERY_REQ/RESP` | Query chain-wide revenue, order counts per location |
| `LIST_LOCATIONS_REQ/RESP` | List all registered locations and their online status |
| `ADMIN_LOGOUT_REQ` | End admin session |

## Project Structure

```
DineSync/
├── src/
│   ├── data_model.h          # Shared data types (MenuItem, Menu, Orders, …)
│   ├── tcp_transport.h       # sendMessage() / recvMessage() — 4-byte framing
│   ├── protocol_messages.h   # P1 client ↔ location-server message builders
│   ├── protocol_handler.h    # P1 server-side message dispatch
│   ├── hub_protocol.h        # P2 Hub message builders (all new msg types)
│   └── hub_server.cpp        # Central Hub Server — main entry point
├── test/
│   └── test_p2.cpp           # 48-test integration suite (embedded Hub)
├── Makefile
└── README.md
```

> **Note:** `data_model.h`, `tcp_transport.h`, `protocol_messages.h`, and `protocol_handler.h` are carried forward from Project 1 and must be copied into `src/` before building.

## Build

```bash
# Build Hub server + test binary
make

# Run integration tests (48 tests)
make test

# Build Hub server only
make hub_server

# Clean
make clean
```

**Requirements:** g++ with C++17 support, POSIX sockets (Linux / macOS).

## Running the Hub

```bash
./hub_server 6000
```

The Hub listens on the given port. Location Servers connect to it on startup; Chain Admin clients connect with `ADMIN_LOGIN_REQ`.

**Default admin credentials:**
- Username: `chain_admin`
- Password: `admin2026`

## Test Results

The integration suite (`test/test_p2.cpp`) embeds a Hub in-process and runs **48 tests** across 5 groups:

| Group | Tests | Coverage |
|---|---|---|
| Hub Protocol Messages | 14 | JSON marshaling for all new message types |
| Location Registration + Menu Sync | 7 | Register, ID assignment, 16-item menu delivery |
| Admin Menu Update + Push | 9 | Auth, token, price update, push to locations |
| Order Reporting + Aggregate Query | 11 | Two-location reporting, chain-wide totals |
| Multiple Simultaneous Locations | 7 | Three concurrent registrations, unique IDs |

```
RESULTS: 48/48 tests passed
ALL TESTS PASSED
```

## Course Info

**ECE 470 — Spring 2026** | Project 2 (Part 3)
Yousef Alhindi — C24090063 | Dr. Nigel John
