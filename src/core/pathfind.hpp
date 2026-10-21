// Shortest paths inside a walkable polygon (with holes).
//
// Classic adventure-game approach: the shortest path between two points in a
// polygon only ever bends at *concave* corners of the outer boundary or
// *convex* corners of a hole. So those corners are the graph nodes; two nodes
// are joined when the straight segment between them stays inside the area
// (the "visibility graph"). Start and goal are linked in per query and A*
// runs with straight-line distance as the (admissible) heuristic.
#pragma once
#include <vector>

#include "core/geom.hpp"

namespace qe {

class Pathfinder {
public:
    Pathfinder() = default;
    explicit Pathfinder(WalkArea area);

    bool inside(Vec2 p) const;
    // p itself if walkable, else the nearest point on the boundary.
    Vec2 clampInside(Vec2 p) const;
    bool lineOfSight(Vec2 a, Vec2 b) const;

    // Waypoints after `from`, ending at the (clamped) goal. Empty if the goal
    // equals the start or cannot be reached.
    std::vector<Vec2> findPath(Vec2 from, Vec2 to) const;

    const WalkArea& area() const { return area_; }
    const std::vector<Vec2>& nodes() const { return nodes_; }
    const std::vector<std::vector<int>>& edges() const { return edges_; }

private:
    WalkArea area_;
    std::vector<Vec2> nodes_;
    std::vector<std::vector<int>> edges_;  // static visibility graph
};

}  // namespace qe
