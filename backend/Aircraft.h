/**
 * Aircraft.h - Aircraft State & Kinematics Model
 *
 * Each aircraft maintains its current kinematic state (position, altitude,
 * speed, heading) and target values set by ATC commands. The update() method
 * applies standard-rate turns (3°/s), standard climb/descent (1500 fpm),
 * and acceleration toward target speed each simulation tick.
 */

#pragma once
#include <string>
#include <vector>

/** A single lat/lon snapshot for the history trail. */
struct HistoryPoint {
    double lat;
    double lon;
};

class Aircraft {
public:
    /* ── Identity ─────────────────────────────────────────────────── */
    std::string callsign;

    /* ── Current kinematic state ──────────────────────────────────── */
    double latitude;          // degrees
    double longitude;         // degrees
    double altitude;          // feet MSL
    double speed;             // knots (ground speed)
    double heading;           // degrees [0, 360)

    /* ── Controller-assigned targets ─────────────────────────────── */
    double targetHeading;
    double targetAltitude;
    double targetSpeed;

    /* ── Position history trail (last N positions) ───────────────── */
    std::vector<HistoryPoint> history;
    static constexpr int MAX_HISTORY = 5;

    /* ── Kinematic constants ─────────────────────────────────────── */
    static constexpr double STANDARD_RATE_TURN = 3.0;   // °/second
    static constexpr double CLIMB_RATE         = 1500.0; // ft/min
    static constexpr double DESCENT_RATE       = 1500.0; // ft/min
    static constexpr double ACCEL_RATE         = 5.0;    // knots/second

    /* ── History sampling ────────────────────────────────────────── */
    double historyTimer = 0.0;
    static constexpr double HISTORY_INTERVAL = 5.0; // seconds between samples

    /** Construct an aircraft with initial state. Targets default to current values. */
    Aircraft(const std::string& cs, double lat, double lon,
             double alt, double spd, double hdg);

    /** Advance the aircraft state by dt seconds. */
    void update(double dt);

    /** Record the current position into the history trail. */
    void addHistoryPoint();
};
