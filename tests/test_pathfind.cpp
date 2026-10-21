#include <cmath>
#include <cstdint>
#include <limits>
#include <queue>

#include "check.hpp"
#include "core/pathfind.hpp"

using namespace qe;

namespace {

bool near(float a, float b, float eps = 1e-2f) { return std::abs(a - b) <= eps; }

float pathLength(Vec2 from, const std::vector<Vec2>& path) {
    float len = 0;
    for (Vec2 p : path) len += dist(from, p), from = p;
    return len;
}

bool pathValid(const Pathfinder& pf, Vec2 from, const std::vector<Vec2>& path) {
    for (Vec2 p : path) {
        if (!pf.lineOfSight(from, p)) return false;
        from = p;
    }
    return true;
}

// U shape opening downwards: two legs joined by a bar across the top.
WalkArea uShape() {
    return {{{0, 0}, {100, 0}, {100, 100}, {70, 100}, {70, 30}, {30, 30}, {30, 100}, {0, 100}}, {}};
}
WalkArea squareWithHole() {
    return {{{0, 0}, {200, 0}, {200, 100}, {0, 100}}, {{{80, 20}, {120, 20}, {120, 80}, {80, 80}}}};
}
WalkArea comb() {  // three teeth hanging down, plus two holes
    return {{{0, 0}, {300, 0}, {300, 200}, {260, 200}, {260, 60}, {200, 60}, {200, 200}, {160, 200}, {160, 60},
             {100, 60}, {100, 200}, {60, 200}, {60, 60}, {40, 60}, {40, 200}, {0, 200}},
            {{{120, 10}, {140, 10}, {140, 40}, {120, 40}}, {{220, 15}, {245, 30}, {225, 45}}}};
}

// Exact reference: Dijkstra over *every* polygon vertex plus start and goal.
float referenceLength(const Pathfinder& pf, Vec2 a, Vec2 b) {
    std::vector<Vec2> pts{a, b};
    for (Vec2 p : pf.area().outer) pts.push_back(p);
    for (auto& h : pf.area().holes)
        for (Vec2 p : h) pts.push_back(p);
    std::vector<float> d(pts.size(), std::numeric_limits<float>::max());
    std::vector<char> done(pts.size(), 0);
    d[0] = 0;
    for (size_t it = 0; it < pts.size(); ++it) {
        size_t u = pts.size();
        for (size_t i = 0; i < pts.size(); ++i)
            if (!done[i] && (u == pts.size() || d[i] < d[u])) u = i;
        if (u == pts.size() || d[u] == std::numeric_limits<float>::max()) break;
        done[u] = 1;
        for (size_t v = 0; v < pts.size(); ++v)
            if (!done[v] && pf.lineOfSight(pts[u], pts[v])) d[v] = std::min(d[v], d[u] + dist(pts[u], pts[v]));
    }
    return d[1];
}

}  // namespace

TEST("pathfind: straight line when the goal is visible") {
    Pathfinder pf({{{0, 0}, {100, 0}, {100, 100}, {0, 100}}, {}});
    auto p = pf.findPath({10, 10}, {90, 80});
    CHECK(p.size() == 1);
    CHECK(p.size() == 1 && p[0] == Vec2{90, 80});
    CHECK(pf.findPath({10, 10}, {10, 10}).empty());
}

TEST("pathfind: bends only at the concave corners of a U") {
    Pathfinder pf(uShape());
    CHECK(pf.nodes().size() == 2);  // (70,30) and (30,30)
    CHECK(!pf.lineOfSight({15, 90}, {85, 90}));
    auto p = pf.findPath({15, 90}, {85, 90});
    CHECK(p.size() == 3);
    CHECK(near(pathLength({15, 90}, p), 2 * std::sqrt(15.f * 15 + 60 * 60) + 40));
    CHECK(pathValid(pf, {15, 90}, p));
}

TEST("pathfind: goes around a hole by its convex corners") {
    Pathfinder pf(squareWithHole());
    CHECK(pf.nodes().size() == 4);
    CHECK(!pf.inside({100, 50}));
    CHECK(pf.inside({80, 50}));  // hole edge is walkable
    auto p = pf.findPath({50, 50}, {150, 50});
    CHECK(near(pathLength({50, 50}, p), 2 * std::sqrt(30.f * 30 + 30 * 30) + 40));
    CHECK(pathValid(pf, {50, 50}, p));
}

TEST("pathfind: walking along a boundary edge is allowed") {
    Pathfinder pf(uShape());
    CHECK(pf.lineOfSight({30, 30}, {70, 30}));
    CHECK(pf.lineOfSight({0, 0}, {0, 100}));
    CHECK(pf.lineOfSight({30, 30}, {30, 100}));
}

TEST("pathfind: a goal outside the area is clamped to the boundary") {
    Pathfinder pf(uShape());
    Vec2 c = pf.clampInside({50, 80});  // in the gap between the legs
    CHECK(pf.inside(c));
    CHECK(near(c.y, 30) || near(c.x, 30) || near(c.x, 70));
    auto p = pf.findPath({15, 90}, {50, 80});
    CHECK(!p.empty() && pf.inside(p.back()));
}

TEST("pathfind: matches exact all-vertex Dijkstra on random queries") {
    uint32_t seed = 12345;
    auto rnd = [&](float lo, float hi) {
        seed = seed * 1664525u + 1013904223u;
        return lo + (hi - lo) * float(seed >> 8) / float(1u << 24);
    };
    int compared = 0, mismatches = 0, invalid = 0;
    for (const WalkArea& area : {uShape(), squareWithHole(), comb()}) {
        Pathfinder pf(area);
        float maxX = 0, maxY = 0;
        for (Vec2 v : area.outer) maxX = std::max(maxX, v.x), maxY = std::max(maxY, v.y);
        for (int i = 0; i < 300; ++i) {
            Vec2 a{rnd(0, maxX), rnd(0, maxY)}, b{rnd(0, maxX), rnd(0, maxY)};
            if (!pf.inside(a) || !pf.inside(b)) continue;
            auto p = pf.findPath(a, b);
            if (!pathValid(pf, a, p)) ++invalid;
            if (!near(pathLength(a, p), referenceLength(pf, a, b), 1e-2f)) ++mismatches;
            ++compared;
        }
    }
    std::printf("  compared %d random paths\n", compared);
    CHECK(compared > 300);
    CHECK(mismatches == 0);
    CHECK(invalid == 0);
}
