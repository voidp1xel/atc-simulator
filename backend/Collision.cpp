/**
 * Collision.cpp - Quadtree Implementation
 *
 * The quadtree partitions 2-D lat/lon space recursively. For conflict
 * detection, each aircraft queries the tree for neighbours within a
 * bounding box sized to the separation minimum (~3 nm ≈ 0.05° lat).
 * Candidate pairs are then checked with exact Haversine distance and
 * altitude difference.
 */

#include "Collision.h"
#include "Aircraft.h"
#include "PhysicsMath.h"
#include <cmath>
#include <set>

/* ── BoundingBox ─────────────────────────────────────────────────────── */

bool BoundingBox::contains(double lat, double lon) const {
    return lat >= (centerLat - halfHeight) &&
           lat <= (centerLat + halfHeight) &&
           lon >= (centerLon - halfWidth)  &&
           lon <= (centerLon + halfWidth);
}

bool BoundingBox::intersects(const BoundingBox& other) const {
    return !(other.centerLon - other.halfWidth  > centerLon + halfWidth  ||
             other.centerLon + other.halfWidth  < centerLon - halfWidth  ||
             other.centerLat - other.halfHeight > centerLat + halfHeight ||
             other.centerLat + other.halfHeight < centerLat - halfHeight);
}

/* ── Quadtree ────────────────────────────────────────────────────────── */

Quadtree::Quadtree(const BoundingBox& bounds) : boundary(bounds) {}

void Quadtree::insert(Aircraft* ac) {
    // Reject if outside this node's boundary
    if (!boundary.contains(ac->latitude, ac->longitude)) return;

    // If there is room and we haven't subdivided, store here
    if ((int)points.size() < CAPACITY && !divided) {
        points.push_back(ac);
        return;
    }

    // Subdivide on first overflow
    if (!divided) subdivide();

    // Delegate to children
    ne->insert(ac);
    nw->insert(ac);
    se->insert(ac);
    sw->insert(ac);
}

void Quadtree::query(const BoundingBox& range,
                     std::vector<Aircraft*>& found) const {
    // Prune branches that don't overlap the search range
    if (!boundary.intersects(range)) return;

    // Check points stored in this node
    for (auto* ac : points) {
        if (range.contains(ac->latitude, ac->longitude)) {
            found.push_back(ac);
        }
    }

    // Recurse into children
    if (divided) {
        ne->query(range, found);
        nw->query(range, found);
        se->query(range, found);
        sw->query(range, found);
    }
}

void Quadtree::clear() {
    points.clear();
    divided = false;
    ne.reset();
    nw.reset();
    se.reset();
    sw.reset();
}

void Quadtree::subdivide() {
    double hW = boundary.halfWidth  / 2.0;
    double hH = boundary.halfHeight / 2.0;
    double cLat = boundary.centerLat;
    double cLon = boundary.centerLon;

    ne = std::make_unique<Quadtree>(BoundingBox{cLat + hH, cLon + hW, hW, hH});
    nw = std::make_unique<Quadtree>(BoundingBox{cLat + hH, cLon - hW, hW, hH});
    se = std::make_unique<Quadtree>(BoundingBox{cLat - hH, cLon + hW, hW, hH});
    sw = std::make_unique<Quadtree>(BoundingBox{cLat - hH, cLon - hW, hW, hH});

    // Re-insert existing points into children
    for (auto* ac : points) {
        ne->insert(ac);
        nw->insert(ac);
        se->insert(ac);
        sw->insert(ac);
    }
    points.clear();
    divided = true;
}

std::vector<ConflictPair> Quadtree::checkConflicts(
    Quadtree& tree, const std::vector<Aircraft*>& all)
{
    std::vector<ConflictPair> conflicts;

    // Use a set of sorted callsign pairs to avoid duplicate reports
    std::set<std::pair<std::string,std::string>> seen;

    // Search radius: 3 nm ≈ 0.05° latitude. Use generous 0.1° box
    constexpr double QUERY_HALF = 0.1;

    for (auto* ac : all) {
        // Build a search box centred on this aircraft
        BoundingBox searchBox{ac->latitude, ac->longitude,
                              QUERY_HALF, QUERY_HALF};

        std::vector<Aircraft*> nearby;
        tree.query(searchBox, nearby);

        for (auto* other : nearby) {
            if (ac == other) continue;

            // Canonical pair key to avoid double-counting
            auto key = (ac->callsign < other->callsign)
                ? std::make_pair(ac->callsign, other->callsign)
                : std::make_pair(other->callsign, ac->callsign);

            if (seen.count(key)) continue;
            seen.insert(key);

            // Exact check: Haversine lateral distance
            double latDist = PhysicsMath::haversineDistance(
                ac->latitude, ac->longitude,
                other->latitude, other->longitude);

            double vertDist = std::abs(ac->altitude - other->altitude);

            if (latDist < SEPARATION_LATERAL_NM &&
                vertDist < SEPARATION_VERTICAL_FT) {
                conflicts.push_back({
                    ac->callsign, other->callsign, latDist, vertDist
                });
            }
        }
    }

    return conflicts;
}
