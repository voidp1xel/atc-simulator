# ATC Sector Simulator

A **real-time Air Traffic Control sector simulator** built entirely from scratch—no external APIs, no game engines. The C++ backend runs a physics-accurate simulation while the vanilla HTML5/Canvas frontend renders a live ATC radar display.

![Architecture](https://img.shields.io/badge/Backend-C++17-blue?style=flat-square) ![Frontend](https://img.shields.io/badge/Frontend-Vanilla_JS_Canvas-green?style=flat-square) ![Protocol](https://img.shields.io/badge/Protocol-WebSocket_10Hz-orange?style=flat-square)

---

## Architecture Overview

```
┌─────────────────────┐       WebSocket (10 Hz)       ┌────────────────────┐
│    C++ Backend       │ ◄──────── JSON ──────────────► │  Browser Frontend  │
│                      │                                │                    │
│  SimulationEngine    │    Telemetry: aircraft state,  │  Canvas radar      │
│  ├── Aircraft[]      │    positions, conflicts        │  ├── Blips         │
│  ├── Quadtree        │                                │  ├── Data blocks   │
│  └── PhysicsMath     │    Commands: heading,          │  ├── Sweep line    │
│                      │    altitude, speed             │  └── Range rings   │
│  Server (HTTP + WS)  │                                │                    │
└─────────────────────┘                                └────────────────────┘
```

### Key Components

| Module | File(s) | Description |
|--------|---------|-------------|
| **Physics** | `PhysicsMath.h/cpp` | Haversine distance, bearing, great-circle destination point |
| **Aircraft** | `Aircraft.h/cpp` | Kinematic state, standard-rate turns (3°/s), climb/descent |
| **Collision** | `Collision.h/cpp` | Quadtree spatial index, loss-of-separation detection |
| **Engine** | `SimulationEngine.h/cpp` | 20 Hz fixed-timestep loop, command queue, JSON telemetry |
| **Server** | `Server.h/cpp` | HTTP static files + WebSocket (RFC 6455), inline SHA-1/Base64 |
| **Radar** | `radar.js` | Canvas rendering: sweep, rings, blips, trails, conflicts |
| **Network** | `network.js` | WebSocket client with auto-reconnect |
| **Controller** | `main.js` | Click selection, command parsing, UI updates |

---

## Prerequisites

### Compiler
- **g++** or **clang++** with C++17 support
- macOS: Xcode Command Line Tools (`xcode-select --install`)
- Linux: `sudo apt install build-essential` (Ubuntu/Debian)

### No external libraries required!
The server uses POSIX sockets and the WebSocket handshake is implemented inline with custom SHA-1 and Base64.

---

## Build & Run

```bash
# 1. Clone/navigate to the project
cd atc-simulator

# 2. Build
make

# 3. Run (default port 8080)
./atc-simulator

# Or specify a custom port
./atc-simulator 9090
```

Then open **http://localhost:8080** in your browser.

### Clean build artifacts

```bash
make clean
```

---

## Using the Simulator

### Radar Display
- **Sweeping line** with a conical phosphor gradient rotates every 10 seconds.
- **Range rings** appear at 10 nm intervals, adjusting dynamically when you zoom.
- **Scroll Zooming**: Use the mouse scroll wheel (or `+`/`-` keys) to zoom in and out of the sector.
- **Hover Introspection**: Hover your mouse over any aircraft blip to instantly view its callsign, altitude context, and speed in a tooltip.
- **Waypoint fixes** are shown as small triangles (JFK, LGA, EWR, MERIT, etc.).
- **Aircraft blips** appear as glowing green dots with data blocks showing:
  - Callsign (e.g., AAL123)
  - Altitude in hundreds of feet (e.g., 350 = FL350 / 35,000 ft) + **Trend arrows** (↑/↓) showing climb/descent
  - Ground speed in knots
- **History trails** show the last recorded positions as fading green dots.

### Selecting Aircraft
Click on any aircraft blip on the radar. The selected aircraft will:
- Highlight with a rotating **cyan** target ring
- Show detailed telemetry and target variables in the right sidebar
- Enable the command input field and **Quick Action** buttons

### Issuing Commands (Vectoring)

You can vector aircraft using either the **Quick Action Buttons** (which allow one-touch heading, altitude, and speed modifications) or by typing commands and pressing **Enter**:

| Command | Example | Effect |
|---------|---------|--------|
| `heading <deg>` | `heading 270` | Vector to heading 270° |
| `altitude <FL>` | `altitude 350` | Climb/descend to FL350 |
| `climb <FL>` | `climb 280` | Climb to FL280 |
| `descend <FL>` | `descend 180` | Descend to FL180 |
| `speed <kts>` | `speed 250` | Adjust to 250 knots |

Aircraft respond with **realistic dynamics**:
- Turns at 3°/second (standard rate)
- Climbs/descends at 1,500 ft/min
- Accelerates at 5 knots/second
*(Note: A dashed cyan line will visualize the aircraft's intended target vector while turning).*

### Keyboard Shortcuts
- `?` - Toggle the **Help Tutorial** overlay
- `Esc` - Deselect standard aircraft and clear focus
- `+` / `-` - Zoom in and out

### Conflicts
When two aircraft violate separation minima (**< 3 nm lateral AND < 1,000 ft vertical**):
- Both blips pulse **red**
- A flashing dashed line connects them showing exact range loss
- The conflict panel on the right lists all active conflicts
- The status bar flashes a conflict count

---

## Project Structure

```
atc-simulator/
├── Makefile                    # Build system
├── README.md                   # This file
├── backend/
│   ├── main.cpp                # Entry point, starts engine + server
│   ├── Server.h / Server.cpp   # HTTP + WebSocket server (POSIX sockets)
│   ├── SimulationEngine.h/cpp  # 20 Hz physics loop, command processing
│   ├── Aircraft.h / Aircraft.cpp   # Aircraft state & kinematics
│   ├── PhysicsMath.h / PhysicsMath.cpp # Haversine, bearing, destination
│   └── Collision.h / Collision.cpp     # Quadtree + separation checking
└── frontend/
    ├── index.html              # Page structure
    ├── style.css               # Dark phosphor-green ATC theme
    ├── network.js              # WebSocket client
    ├── radar.js                # Canvas radar rendering
    └── main.js                 # UI controller & command parsing
```

---

## Technical Details

### Simulation Timing
- **Physics tick rate**: 20 Hz (50 ms fixed timestep)
- **Network broadcast**: 10 Hz (100 ms between telemetry frames)
- **Radar sweep**: 6 RPM (one full rotation every 10 seconds)

### Geodesic Math
All position calculations use the **Haversine formula** for accuracy on a spherical Earth:
- Distance: `d = R · 2·atan2(√a, √(1−a))` where `a = sin²(Δlat/2) + cos(lat1)·cos(lat2)·sin²(Δlon/2)`
- Destination: spherical-law forward projection given start point, bearing, and distance

### Quadtree Collision Detection
The spatial index uses a **recursive quadtree** over lat/lon space:
1. All aircraft are inserted each tick
2. For each aircraft, a bounding-box query finds nearby candidates (~0.1° ≈ 6 nm box)
3. Candidate pairs are checked with exact Haversine distance + altitude difference
4. Violations of 3 nm lateral / 1,000 ft vertical are flagged as conflicts

---

## License

This project is provided as-is for educational and simulation purposes.
