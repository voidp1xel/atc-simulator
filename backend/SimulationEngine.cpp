/**
 * SimulationEngine.cpp - Simulation Loop Implementation
 *
 * The simulation runs at 20 Hz (50 ms fixed timestep) on its own thread.
 * Each tick:
 *   1. Drains the command queue and applies commands to aircraft.
 *   2. Updates every aircraft's kinematics (heading, alt, speed, position).
 *   3. Rebuilds the spatial quadtree and checks for separation conflicts.
 *   4. Removes aircraft that have left the sector and spawns replacements.
 *
 * The getTelemetryJSON() method is called by the network thread and
 * is protected by a mutex so it always reads a consistent snapshot.
 */

#include "SimulationEngine.h"
#include "PhysicsMath.h"
#include <thread>
#include <chrono>
#include <sstream>
#include <iostream>
#include <iomanip>
#include <algorithm>
#include <cstdlib>
#include <ctime>

/* ══════════════════════════════════════════════════════════════════════
 *  Construction / Destruction
 * ══════════════════════════════════════════════════════════════════════ */

SimulationEngine::SimulationEngine() {
    std::srand(static_cast<unsigned>(std::time(nullptr)));
}

SimulationEngine::~SimulationEngine() { stop(); }

/* ══════════════════════════════════════════════════════════════════════
 *  Lifecycle
 * ══════════════════════════════════════════════════════════════════════ */

void SimulationEngine::start() {
    spawnInitialAircraft();
    running = true;
    std::thread t(&SimulationEngine::simulationLoop, this);
    t.detach();
    std::cout << "[SimEngine] Started at " << TICK_RATE << " Hz\n";
}

void SimulationEngine::stop() {
    running = false;
}

/* ══════════════════════════════════════════════════════════════════════
 *  Main simulation loop (runs on dedicated thread)
 * ══════════════════════════════════════════════════════════════════════ */

void SimulationEngine::simulationLoop() {
    using clock = std::chrono::steady_clock;
    auto nextTick = clock::now();

    while (running) {
        processCommands();

        {
            std::lock_guard<std::mutex> lock(stateMutex);
            updateAircraft();
            checkCollisions();
            respawnIfNeeded();
            simulationTime += DT;
        }

        // Fixed timestep: sleep until next tick
        nextTick += std::chrono::microseconds(static_cast<long>(DT * 1e6));
        std::this_thread::sleep_until(nextTick);
    }
}

/* ══════════════════════════════════════════════════════════════════════
 *  Command processing (drain the thread-safe queue)
 * ══════════════════════════════════════════════════════════════════════ */

void SimulationEngine::processCommands() {
    std::lock_guard<std::mutex> cmdLock(commandMutex);

    while (!commandQueue.empty()) {
        Command cmd = commandQueue.front();
        commandQueue.pop();

        std::lock_guard<std::mutex> stateLock(stateMutex);
        for (auto& ac : aircraft) {
            if (ac->callsign == cmd.callsign) {
                if (cmd.action == "heading") {
                    ac->targetHeading = PhysicsMath::normalizeHeading(cmd.value);
                    std::cout << "[CMD] " << cmd.callsign
                              << " → heading " << cmd.value << "°\n";
                } else if (cmd.action == "altitude") {
                    ac->targetAltitude = cmd.value;
                    std::cout << "[CMD] " << cmd.callsign
                              << " → altitude " << cmd.value << " ft\n";
                } else if (cmd.action == "speed") {
                    ac->targetSpeed = cmd.value;
                    std::cout << "[CMD] " << cmd.callsign
                              << " → speed " << cmd.value << " kts\n";
                }
                break;
            }
        }
    }
}

void SimulationEngine::enqueueCommand(const Command& cmd) {
    std::lock_guard<std::mutex> lock(commandMutex);
    commandQueue.push(cmd);
}

/* ══════════════════════════════════════════════════════════════════════
 *  Aircraft update & collision
 * ══════════════════════════════════════════════════════════════════════ */

void SimulationEngine::updateAircraft() {
    for (auto& ac : aircraft) {
        ac->update(DT);
    }
}

void SimulationEngine::checkCollisions() {
    // Build quadtree covering the entire sector (generous bounds)
    BoundingBox sectorBox{CENTER_LAT, CENTER_LON, 2.0, 2.0};
    Quadtree tree(sectorBox);

    std::vector<Aircraft*> ptrs;
    for (auto& ac : aircraft) {
        ptrs.push_back(ac.get());
        tree.insert(ac.get());
    }

    conflicts = Quadtree::checkConflicts(tree, ptrs);

    if (!conflicts.empty()) {
        for (auto& c : conflicts) {
            std::cout << "[CONFLICT] " << c.callsignA << " – " << c.callsignB
                      << "  lat=" << std::fixed << std::setprecision(1)
                      << c.lateralDistNm << " nm  vert="
                      << c.verticalDistFt << " ft\n";
        }
    }
}

/* ══════════════════════════════════════════════════════════════════════
 *  Spawning
 * ══════════════════════════════════════════════════════════════════════ */

void SimulationEngine::spawnInitialAircraft() {
    std::lock_guard<std::mutex> lock(stateMutex);

    // Eight aircraft at various positions, altitudes, and headings
    // around the New York TRACON area
    aircraft.push_back(std::make_unique<Aircraft>(
        "AAL123", 41.20, -73.20, 35000, 450, 220));   // from NE
    aircraft.push_back(std::make_unique<Aircraft>(
        "UAL456", 40.10, -74.50, 28000, 380,  45));   // from SW
    aircraft.push_back(std::make_unique<Aircraft>(
        "DAL789", 41.00, -74.00, 32000, 420, 180));   // from N
    aircraft.push_back(std::make_unique<Aircraft>(
        "SWA321", 40.20, -73.00, 24000, 350, 270));   // from E
    aircraft.push_back(std::make_unique<Aircraft>(
        "JBU567", 40.80, -73.50, 18000, 300, 200));   // mid-sector
    aircraft.push_back(std::make_unique<Aircraft>(
        "FDX890", 40.40, -74.20, 37000, 480,  90));   // from W
    aircraft.push_back(std::make_unique<Aircraft>(
        "NKS234", 41.10, -73.80, 22000, 340, 190));   // from N
    aircraft.push_back(std::make_unique<Aircraft>(
        "BAW100", 40.30, -73.30, 39000, 500, 300));   // from SE
}

void SimulationEngine::respawnIfNeeded() {
    // Remove aircraft that have left the sector (> SECTOR_RADIUS_NM from centre)
    // and spawn a fresh replacement on the opposite edge.
    static const std::vector<std::string> CALLSIGNS = {
        "AAL", "UAL", "DAL", "SWA", "JBU", "FDX", "NKS", "BAW",
        "EJA", "SKW", "RPA", "ASA", "FFT", "AWE", "VRD", "ENY"
    };

    auto it = aircraft.begin();
    while (it != aircraft.end()) {
        double dist = PhysicsMath::haversineDistance(
            CENTER_LAT, CENTER_LON,
            (*it)->latitude, (*it)->longitude);

        if (dist > SECTOR_RADIUS_NM * 1.2) {
            std::cout << "[SimEngine] " << (*it)->callsign
                      << " exited sector, removing.\n";
            it = aircraft.erase(it);
        } else {
            ++it;
        }
    }

    // Keep at least 6 aircraft in the sector
    while ((int)aircraft.size() < 6) {
        // Random edge position
        double angle = (std::rand() % 360) * PhysicsMath::DEG_TO_RAD;
        double edgeDist = SECTOR_RADIUS_NM * 0.8;  // spawn inside edge

        double lat, lon;
        PhysicsMath::destinationPoint(CENTER_LAT, CENTER_LON,
            angle * PhysicsMath::RAD_TO_DEG, edgeDist, lat, lon);

        // Heading toward centre
        double hdg = PhysicsMath::bearing(lat, lon, CENTER_LAT, CENTER_LON);
        // Random offset
        hdg = PhysicsMath::normalizeHeading(hdg + (std::rand() % 40 - 20));

        double alt = 15000 + (std::rand() % 26) * 1000;   // FL150–FL400
        double spd = 250 + (std::rand() % 250);           // 250–500 kts

        std::string cs = CALLSIGNS[std::rand() % CALLSIGNS.size()]
                         + std::to_string(100 + std::rand() % 900);

        aircraft.push_back(std::make_unique<Aircraft>(cs, lat, lon, alt, spd, hdg));
        std::cout << "[SimEngine] Spawned " << cs << "\n";
    }
}

/* ══════════════════════════════════════════════════════════════════════
 *  Telemetry JSON serialisation (called from network thread)
 * ══════════════════════════════════════════════════════════════════════ */

std::string SimulationEngine::escapeJSON(const std::string& s) {
    std::string out;
    for (char c : s) {
        if (c == '"')       out += "\\\"";
        else if (c == '\\') out += "\\\\";
        else                out += c;
    }
    return out;
}

std::string SimulationEngine::getTelemetryJSON() {
    std::lock_guard<std::mutex> lock(stateMutex);

    std::ostringstream ss;
    ss << std::fixed << std::setprecision(6);

    ss << "{\"type\":\"telemetry\","
       << "\"time\":" << std::setprecision(2) << simulationTime << ","
       << "\"aircraft\":[";

    for (size_t i = 0; i < aircraft.size(); ++i) {
        auto& ac = aircraft[i];
        if (i > 0) ss << ",";

        ss << "{\"cs\":\"" << escapeJSON(ac->callsign) << "\","
           << "\"lat\":" << std::setprecision(6) << ac->latitude << ","
           << "\"lon\":" << std::setprecision(6) << ac->longitude << ","
           << "\"alt\":" << std::setprecision(0) << ac->altitude << ","
           << "\"spd\":" << std::setprecision(0) << ac->speed << ","
           << "\"hdg\":" << std::setprecision(1) << ac->heading << ","
           << "\"thdg\":" << std::setprecision(1) << ac->targetHeading << ","
           << "\"talt\":" << std::setprecision(0) << ac->targetAltitude << ","
           << "\"tspd\":" << std::setprecision(0) << ac->targetSpeed << ","
           << "\"hist\":[";

        for (size_t h = 0; h < ac->history.size(); ++h) {
            if (h > 0) ss << ",";
            ss << "[" << std::setprecision(6) << ac->history[h].lat
               << "," << ac->history[h].lon << "]";
        }
        ss << "]}";
    }

    ss << "],\"conflicts\":[";
    for (size_t i = 0; i < conflicts.size(); ++i) {
        if (i > 0) ss << ",";
        ss << "{\"a\":\"" << escapeJSON(conflicts[i].callsignA) << "\","
           << "\"b\":\"" << escapeJSON(conflicts[i].callsignB) << "\","
           << "\"lat\":" << std::setprecision(2) << conflicts[i].lateralDistNm << ","
           << "\"vert\":" << std::setprecision(0) << conflicts[i].verticalDistFt << "}";
    }
    ss << "]}";

    return ss.str();
}
