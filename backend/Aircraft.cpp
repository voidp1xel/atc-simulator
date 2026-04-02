/**
 * Aircraft.cpp - Aircraft Kinematics Implementation
 *
 * Update loop applies three independent control axes:
 *   1. Heading  → standard-rate turn (3°/s) toward targetHeading
 *   2. Altitude → climb/descend at 1500 fpm toward targetAltitude
 *   3. Speed    → accelerate/decelerate at 5 kts/s toward targetSpeed
 *
 * Position is then advanced along the current heading using the
 * great-circle destination-point formula from PhysicsMath.
 */

#include "Aircraft.h"
#include "PhysicsMath.h"
#include <cmath>

Aircraft::Aircraft(const std::string& cs, double lat, double lon,
                   double alt, double spd, double hdg)
    : callsign(cs), latitude(lat), longitude(lon),
      altitude(alt), speed(spd), heading(hdg),
      targetHeading(hdg), targetAltitude(alt), targetSpeed(spd)
{
    // Seed the history trail with the spawn position
    history.push_back({lat, lon});
}

void Aircraft::update(double dt) {
    /* ── 1. Heading: standard-rate turn ──────────────────────────── */
    double hdgDiff = PhysicsMath::shortestTurnDirection(heading, targetHeading);
    if (std::abs(hdgDiff) > 0.1) {
        double maxTurn = STANDARD_RATE_TURN * dt;          // max degrees this tick
        if (std::abs(hdgDiff) <= maxTurn) {
            heading = targetHeading;                       // snap when close
        } else {
            heading += (hdgDiff > 0 ? 1.0 : -1.0) * maxTurn;
        }
        heading = PhysicsMath::normalizeHeading(heading);
    }

    /* ── 2. Altitude: climb / descend ────────────────────────────── */
    double altDiff = targetAltitude - altitude;
    if (std::abs(altDiff) > 10.0) {
        // Convert fpm to fps: rate / 60, then multiply by dt
        double rate = (altDiff > 0 ? CLIMB_RATE : -DESCENT_RATE);
        double change = rate * dt / 60.0;
        if (std::abs(altDiff) < std::abs(change)) {
            altitude = targetAltitude;
        } else {
            altitude += change;
        }
    }

    /* ── 3. Speed: acceleration / deceleration ───────────────────── */
    double spdDiff = targetSpeed - speed;
    if (std::abs(spdDiff) > 1.0) {
        double change = ACCEL_RATE * dt;
        if (std::abs(spdDiff) < change) {
            speed = targetSpeed;
        } else {
            speed += (spdDiff > 0 ? 1.0 : -1.0) * change;
        }
    }

    /* ── 4. Position: great-circle advance ───────────────────────── */
    // speed is in knots (nm/hr), convert to nm travelled this tick
    double distNm = (speed / 3600.0) * dt;
    double newLat, newLon;
    PhysicsMath::destinationPoint(latitude, longitude, heading,
                                  distNm, newLat, newLon);
    latitude  = newLat;
    longitude = newLon;

    /* ── 5. History trail ────────────────────────────────────────── */
    historyTimer += dt;
    if (historyTimer >= HISTORY_INTERVAL) {
        addHistoryPoint();
        historyTimer = 0.0;
    }
}

void Aircraft::addHistoryPoint() {
    history.push_back({latitude, longitude});
    while ((int)history.size() > MAX_HISTORY) {
        history.erase(history.begin());
    }
}
