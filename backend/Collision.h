/**
 * Collision.h - Quadtree-Based Collision Detection
 *
 * Implements a spatial Quadtree over lat/lon space to efficiently find
 * aircraft pairs that violate separation minima:
 *   - Lateral:  < 3 nautical miles
 *   - Vertical: < 1,000 feet
 *
 * The tree is rebuilt every simulation tick (cheap at typical ATC counts).
 */

#pragma once
#include <vector>
#include <memory>
#include <string>

// Forward declaration to avoid circular include
class Aircraft;

/** Axis-aligned bounding box in lat/lon degrees. */
struct BoundingBox {
    double centerLat, centerLon;
    double halfWidth, halfHeight;   // in degrees

    /** Check if a point lies inside this box. */
    bool contains(double lat, double lon) const;

    /** Check if this box overlaps another box. */
    bool intersects(const BoundingBox& other) const;
};

/** A detected loss-of-separation event between two aircraft. */
struct ConflictPair {
    std::string callsignA;
    std::string callsignB;
    double lateralDistNm;
    double verticalDistFt;
};

/**
 * Quadtree for 2-D spatial indexing of aircraft positions.
 *
 * Each leaf holds up to CAPACITY aircraft pointers. When exceeded,
 * the node subdivides into four children (NE, NW, SE, SW).
 * The query() method returns all aircraft within a search box,
 * enabling O(n·log n) average-case conflict detection instead of O(n²).
 */
class Quadtree {
public:
    static constexpr int    CAPACITY              = 4;
    static constexpr double SEPARATION_LATERAL_NM = 3.0;
    static constexpr double SEPARATION_VERTICAL_FT= 1000.0;

    explicit Quadtree(const BoundingBox& bounds);

    /** Insert an aircraft pointer into the tree. */
    void insert(Aircraft* aircraft);

    /** Find all aircraft within the given bounding box. */
    void query(const BoundingBox& range, std::vector<Aircraft*>& found) const;

    /** Remove all aircraft and collapse subdivisions. */
    void clear();

    /**
     * Check all aircraft for loss of separation using quadtree queries.
     * @param tree   A populated Quadtree.
     * @param all    The full aircraft list (for iteration).
     * @return       Vector of detected conflict pairs.
     */
    static std::vector<ConflictPair> checkConflicts(
        Quadtree& tree, const std::vector<Aircraft*>& all);

private:
    BoundingBox boundary;
    std::vector<Aircraft*> points;
    bool divided = false;

    std::unique_ptr<Quadtree> ne, nw, se, sw;

    /** Split this node into four quadrants and redistribute points. */
    void subdivide();
};
