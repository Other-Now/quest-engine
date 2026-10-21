// 2D geometry used by the walkable-area pathfinder.
#pragma once
#include <cmath>
#include <vector>

namespace qe {

struct Vec2 {
    float x = 0, y = 0;
};
inline Vec2 operator+(Vec2 a, Vec2 b) { return {a.x + b.x, a.y + b.y}; }
inline Vec2 operator-(Vec2 a, Vec2 b) { return {a.x - b.x, a.y - b.y}; }
inline Vec2 operator*(Vec2 a, float s) { return {a.x * s, a.y * s}; }
inline bool operator==(Vec2 a, Vec2 b) { return a.x == b.x && a.y == b.y; }
inline float dot(Vec2 a, Vec2 b) { return a.x * b.x + a.y * b.y; }
inline float cross(Vec2 a, Vec2 b) { return a.x * b.y - a.y * b.x; }
inline float length(Vec2 a) { return std::sqrt(dot(a, a)); }
inline float dist(Vec2 a, Vec2 b) { return length(a - b); }

struct Rect {
    float x = 0, y = 0, w = 0, h = 0;
    bool contains(Vec2 p) const { return p.x >= x && p.x < x + w && p.y >= y && p.y < y + h; }
    Vec2 center() const { return {x + w * 0.5f, y + h * 0.5f}; }
};

using Polygon = std::vector<Vec2>;

// One outer boundary plus holes (obstacles such as a well or a table).
// Points on any boundary count as walkable.
struct WalkArea {
    Polygon outer;
    std::vector<Polygon> holes;
};

constexpr float kGeomEps = 1e-3f;

float signedArea(const Polygon& poly);
Vec2 closestPointOnSegment(Vec2 p, Vec2 a, Vec2 b);
bool onBoundary(const Polygon& poly, Vec2 p, float eps = kGeomEps);
// Even-odd test; boundary points count as inside.
bool pointInPolygon(const Polygon& poly, Vec2 p);
// True only for a crossing in the interior of both segments.
bool segmentsCrossProperly(Vec2 a, Vec2 b, Vec2 c, Vec2 d);

}  // namespace qe
