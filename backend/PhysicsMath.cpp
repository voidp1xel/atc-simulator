/**
 * PhysicsMath.cpp - Implementation of geodesic math functions.
 *
 * The Haversine formula is used for great-circle distance on a sphere.
 * It is accurate for short distances typical in ATC sectors (~100 nm).
 */

#include "PhysicsMath.h"

namespace PhysicsMath {

double haversineDistance(double lat1, double lon1, double lat2, double lon2) {
    // Convert degree deltas to radians
    double dLat = (lat2 - lat1) * DEG_TO_RAD;
    double dLon = (lon2 - lon1) * DEG_TO_RAD;

    // Haversine formula core
    double a = std::sin(dLat / 2.0) * std::sin(dLat / 2.0) +
               std::cos(lat1 * DEG_TO_RAD) * std::cos(lat2 * DEG_TO_RAD) *
               std::sin(dLon / 2.0) * std::sin(dLon / 2.0);

    double c = 2.0 * std::atan2(std::sqrt(a), std::sqrt(1.0 - a));
    return EARTH_RADIUS_NM * c;
}

double bearing(double lat1, double lon1, double lat2, double lon2) {
    double dLon  = (lon2 - lon1) * DEG_TO_RAD;
    double lat1R = lat1 * DEG_TO_RAD;
    double lat2R = lat2 * DEG_TO_RAD;

    double y = std::sin(dLon) * std::cos(lat2R);
    double x = std::cos(lat1R) * std::sin(lat2R) -
               std::sin(lat1R) * std::cos(lat2R) * std::cos(dLon);

    return normalizeHeading(std::atan2(y, x) * RAD_TO_DEG);
}

void destinationPoint(double lat, double lon, double heading,
                       double distanceNm, double& newLat, double& newLon) {
    double latR = lat * DEG_TO_RAD;
    double lonR = lon * DEG_TO_RAD;
    double hdgR = heading * DEG_TO_RAD;
    double d    = distanceNm / EARTH_RADIUS_NM;   // angular distance

    // Spherical-law destination formula
    newLat = std::asin(
        std::sin(latR) * std::cos(d) +
        std::cos(latR) * std::sin(d) * std::cos(hdgR));

    newLon = lonR + std::atan2(
        std::sin(hdgR) * std::sin(d) * std::cos(latR),
        std::cos(d) - std::sin(latR) * std::sin(newLat));

    newLat *= RAD_TO_DEG;
    newLon *= RAD_TO_DEG;
}

double normalizeHeading(double heading) {
    heading = std::fmod(heading, 360.0);
    if (heading < 0.0) heading += 360.0;
    return heading;
}

double shortestTurnDirection(double current, double target) {
    double diff = target - current;
    if (diff >  180.0) diff -= 360.0;
    if (diff < -180.0) diff += 360.0;
    return diff;   // positive = clockwise, negative = counter-clockwise
}

} // namespace PhysicsMath
