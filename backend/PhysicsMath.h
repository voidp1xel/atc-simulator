/**
 * PhysicsMath.h - Geodesic Mathematics for ATC Simulation
 * 
 * Provides Haversine-based distance/bearing calculations, great-circle
 * destination point computation, and heading normalization utilities.
 * All angular inputs/outputs are in degrees; distances in nautical miles.
 */

#pragma once
#include <cmath>

namespace PhysicsMath {

    /* ── Constants ────────────────────────────────────────────────────── */
    constexpr double PI               = 3.14159265358979323846;
    constexpr double EARTH_RADIUS_NM  = 3440.065;   // Earth radius in nautical miles
    constexpr double DEG_TO_RAD       = PI / 180.0;
    constexpr double RAD_TO_DEG       = 180.0 / PI;
    constexpr double NM_PER_DEG_LAT   = 60.0;       // 1° latitude ≈ 60 nm

    /**
     * Haversine distance between two geographic coordinates.
     * Uses the Haversine formula:  a = sin²(Δlat/2) + cos(lat1)·cos(lat2)·sin²(Δlon/2)
     *                              c = 2·atan2(√a, √(1−a))
     *                              d = R · c
     * @return Distance in nautical miles.
     */
    double haversineDistance(double lat1, double lon1, double lat2, double lon2);

    /**
     * Initial (forward) bearing from point 1 to point 2.
     * @return Bearing in degrees [0, 360).
     */
    double bearing(double lat1, double lon1, double lat2, double lon2);

    /**
     * Compute destination point given a start, heading, and distance.
     * Uses the spherical-law formula for great-circle navigation.
     * @param[out] newLat  Resulting latitude in degrees.
     * @param[out] newLon  Resulting longitude in degrees.
     */
    void destinationPoint(double lat, double lon, double heading,
                          double distanceNm, double& newLat, double& newLon);

    /** Normalize any heading value to [0, 360). */
    double normalizeHeading(double heading);

    /**
     * Compute the shortest angular difference from current to target heading.
     * @return Positive = clockwise, negative = counter-clockwise.
     */
    double shortestTurnDirection(double current, double target);

} // namespace PhysicsMath
