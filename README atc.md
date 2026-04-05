<div align="center">
  
# 🌌 ATC Sector Simulator ✈️
  
*A real-time Air Traffic Control sector simulator built entirely from scratch.*

![Backend](https://img.shields.io/badge/Backend-C++17-0059b3?style=for-the-badge&logo=c%2B%2B)
![Frontend](https://img.shields.io/badge/Frontend-Vanilla_JS_&_Canvas-f7df1e?style=for-the-badge&logo=javascript)
![Protocol](https://img.shields.io/badge/Protocol-WebSocket_10Hz-ff6600?style=for-the-badge&logo=websocket)
![Zero Dependencies](https://img.shields.io/badge/Dependencies-Zero-44cc11?style=for-the-badge)

</div>

---

## 🚀 Overview

The **ATC Sector Simulator** is a high-fidelity, real-time Air Traffic Control sector simulation. It operates without any external APIs, game engines, or third-party backend frameworks. 

The backend is engineered in **C++17**, creating a robust physics-accurate simulation environment. The frontend is a highly responsive **Vanilla HTML5/Canvas** application that renders a live ATC radar display, receiving telemetry via a custom-built WebSocket server.

---

## 🧠 Code Architecture & Design Details

The project is thoughtfully divided into a performance-critical C++ backend and a dynamic web frontend, linked by a bi-directional WebSocket connection.

```ascii
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

### ⚙️ Backend Module Breakdown

| Module | File(s) | Deep Dive |
|--------|---------|-------------|
| **📐 Physics** | `PhysicsMath.h/cpp` | Implements true geodesic mathematics. Uses the **Haversine formula** to calculate distances on a spherical Earth, along with initial bearing calculations and spherical-law forward projections for accurate trajectory plotting. |
| **✈️ Aircraft** | `Aircraft.h/cpp` | Manages the kinematic state of each flight. Includes logic for standard-rate turns (3°/second), climbs/descents (1,500 ft/min), and linear acceleration (5 knots/sec). Maintains target constraints vs current state. |
| **⚠️ Collision** | `Collision.h/cpp` | Houses a **Recursive Quadtree** spatial index. Every tick, aircraft are inserted into the quadtree to efficiently find proximate traffic. Flags violations of separation minima (3 nm lateral AND 1,000 ft vertical). |
| **⏱️ Engine** | `SimulationEngine.h/cpp` | The core loop running at a fixed **20 Hz timestep** (50 ms). It dequeues controller commands, updates physics steps, detects conflicts, and packages instantaneous JSON telemetry to be broadcasted at 10 Hz. |
| **🌐 Server** | `Server.h/cpp` | A highly optimized custom HTTP + WebSocket server built directly on **POSIX sockets**. Hand-rolled WebSocket (`RFC 6455`) implementation complete with inline SHA-1 hashing and Base64 encoding for the handshake. |

### 🖥️ Frontend Module Breakdown

| Module | File(s) | Deep Dive |
|--------|---------|-------------|
| **📡 Radar** | `radar.js` | The visualization heart. Uses `requestAnimationFrame` for a smooth Canvas 2D render loop. Draws the rotating conical phosphor sweep (10s rotation), scalable range rings, fading history trails, and blinking conflict alerts. |
| **🔌 Network** | `network.js` | Handles the WebSocket client connection, parsing incoming JSON telemetry, and dispatching vector commands to the server. Includes robust auto-reconnect logic. |
| **🎮 Controller** | `main.js` | Drives the UI interactions. Manages aircraft selection via bounding-box canvas clicks, updates the detailed side-panel, binds Quick Action buttons, and handles keyboard shortcuts (like zooming or the Help overlay). |

---

## 🛠️ Prerequisites & Setup

### Compiler
Ensure you have a modern C++ compiler setup.
- **macOS:** Xcode Command Line Tools (`xcode-select --install`)
- **Linux:** `sudo apt install build-essential` (Ubuntu/Debian)

> **No external dependencies required!** 
> Everything, including network sockets and hashing algorithms, is written inline or utilizes standard POSIX/C++17 libraries.

### 🏗️ Build & Run Instructions

```bash
# 1. Clone the repository and navigate into it
cd atc-simulator

# 2. Compile the project using the makefile
make

# 3. Start the server (Defaults to port 8080)
./atc-simulator

# (Optional) Specify a custom port
./atc-simulator 9090
```

Once running, navigate to `http://localhost:8080` in your web browser.

---

## 🎮 Using the Simulator

### 🟢 Radar Display Mechanics
- **Sweeping line:** A dynamic conical phosphor gradient sweeps the screen continuously at 6 RPM.
- **Range rings:** Displayed at 10 nm intervals, adjusting dynamically to your zoom level.
- **Scroll Zooming:** Use the mouse scroll wheel or `+`/`-` keys to fluidly zoom the sector map.
- **Hover Introspection:** Hover over any blip to trigger a tooltip displaying its Callsign, Altitude context, and Speed.
- **Blips & Data Blocks:** Target aircraft are depicted as bright green blips with detailed data tags:
  - **Callsign:** (e.g., `AAL123`)
  - **Altitude:** Hundreds of feet (e.g., `350` = FL350) + **Trend arrows** (↑/↓) for vertical speed.
  - **Speed:** Current ground speed in knots.
- **Trails:** Fading green dots trail each aircraft, visualizing recent physical movements.

### 🎯 Selecting & Vectoring Aircraft
Click on any aircraft blip on the radar. The target will highlight with a rotating **cyan** ring, and its complete telemetry will load in the right-hand sidebar.

You can issue vector commands via the **Quick Action Buttons** or the input field:

| Command Syntax | Example | Action Effect |
|---------|---------|--------|
| `heading <deg>` | `heading 270` | Commands the aircraft to turn to a 270° heading. |
| `altitude <FL>` | `altitude 350` | Commands the aircraft to climb or descend to FL350. |
| `climb <FL>` | `climb 280` | Forces a climb to FL280. |
| `descend <FL>` | `descend 180` | Forces a descent to FL180. |
| `speed <kts>` | `speed 250` | Commands a speed adjustment to 250 knots. |

*Visual Aid: A dashed cyan line dynamically projects the aircraft's intended flight path during a vector adjustment.*

### 🚨 Conflict Resolution
Air traffic control requires strict separation. A conflict occurs when two aircraft breach minima (**< 3 nm lateral AND < 1,000 ft vertical**).
- Both involved aircraft pulse **red**.
- A high-visibility flashing dashed line connects the targets.
- The conflict is logged in the right panel and flashes on the system status bar.

---

## ⚖️ License & Copyright

**Copyright © 2026. All rights reserved.**

This project and its entire source code are my original work. They **cannot be used, copied, reproduced, distributed, or modified** in any form without my express written permission.
