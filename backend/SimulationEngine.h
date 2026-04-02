/**
 * SimulationEngine.h - Core Simulation Loop
 *
 * Runs a fixed-timestep physics loop on a dedicated thread, completely
 * decoupled from the network broadcast rate. Manages the aircraft list,
 * processes queued ATC commands, and rebuilds the collision quadtree
 * every tick.
 */

#pragma once
#include <vector>
#include <memory>
#include <mutex>
#include <atomic>
#include <queue>
#include <string>
#include "Aircraft.h"
#include "Collision.h"

/** A parsed ATC command destined for a specific aircraft. */
struct Command {
    std::string callsign;
    std::string action;   // "heading", "altitude", "speed"
    double value;
};

class SimulationEngine {
public:
    SimulationEngine();
    ~SimulationEngine();

    /** Start the simulation thread. */
    void start();

    /** Signal the simulation thread to stop and join it. */
    void stop();

    /* ── Thread-safe public interface ────────────────────────────── */

    /** Serialize the current world state as a JSON string. */
    std::string getTelemetryJSON();

    /** Push an ATC command into the thread-safe queue. */
    void enqueueCommand(const Command& cmd);

    /* ── Sector parameters ───────────────────────────────────────── */
    static constexpr double CENTER_LAT       = 40.6413;   // JFK area
    static constexpr double CENTER_LON       = -73.7781;
    static constexpr double SECTOR_RADIUS_NM = 60.0;

private:
    /* ── Aircraft storage ────────────────────────────────────────── */
    std::vector<std::unique_ptr<Aircraft>> aircraft;
    std::vector<ConflictPair> conflicts;

    /* ── Thread synchronisation ──────────────────────────────────── */
    std::mutex stateMutex;          // protects aircraft + conflicts
    std::queue<Command> commandQueue;
    std::mutex commandMutex;        // protects commandQueue
    std::atomic<bool> running{false};

    /* ── Timing ──────────────────────────────────────────────────── */
    double simulationTime = 0.0;
    static constexpr double TICK_RATE = 20.0;            // Hz
    static constexpr double DT        = 1.0 / TICK_RATE; // 50 ms

    /* ── Internal methods (called on sim thread) ─────────────────── */
    void simulationLoop();
    void processCommands();
    void updateAircraft();
    void checkCollisions();
    void spawnInitialAircraft();
    void respawnIfNeeded();

    /** Simple JSON helpers (no external library). */
    std::string escapeJSON(const std::string& s);
};
