#include "core/pathfind.hpp"

#include <algorithm>
#include <limits>
#include <queue>

namespace qe {

namespace {

// Corners where a shortest path may bend. For the outer boundary those are the
// reflex (concave) corners; for a hole they are its convex corners, which are
// concave as seen from the walkable side.
void addBendCorners(const Polygon& poly, bool isHole, std::vector<Vec2>& out) {
    size_t n = poly.size();
    if (n < 3) return;
    float orientation = signedArea(poly) > 0 ? 1.0f : -1.0f;
    for (size_t i = 0; i < n; ++i) {
        Vec2 prev = poly[(i + n - 1) % n], cur = poly[i], next = poly[(i + 1) % n];
        float turn = cross(cur - prev, next - cur) * orientation;
        bool convex = turn > 0;
        if (isHole ? convex : !convex) out.push_back(cur);
    }
}

template <class F>
void forEachEdge(const WalkArea& area, F&& f) {
    auto poly = [&](const Polygon& p) {
        for (size_t i = 0, n = p.size(); i < n; ++i) f(p[i], p[(i + 1) % n]);
    };
    poly(area.outer);
    for (auto& h : area.holes) poly(h);
}

}  // namespace

Pathfinder::Pathfinder(WalkArea area) : area_(std::move(area)) {
    addBendCorners(area_.outer, false, nodes_);
    for (auto& h : area_.holes) addBendCorners(h, true, nodes_);
    edges_.assign(nodes_.size(), {});
    for (size_t i = 0; i < nodes_.size(); ++i)
        for (size_t j = i + 1; j < nodes_.size(); ++j)
            if (lineOfSight(nodes_[i], nodes_[j])) {
                edges_[i].push_back(int(j));
                edges_[j].push_back(int(i));
            }
}

bool Pathfinder::inside(Vec2 p) const {
    if (area_.outer.size() < 3 || !pointInPolygon(area_.outer, p)) return false;
    for (auto& h : area_.holes)
        if (pointInPolygon(h, p) && !onBoundary(h, p)) return false;
    return true;
}

Vec2 Pathfinder::clampInside(Vec2 p) const {
    if (inside(p)) return p;
    Vec2 best = p;
    float bestD = std::numeric_limits<float>::max();
    forEachEdge(area_, [&](Vec2 a, Vec2 b) {
        Vec2 c = closestPointOnSegment(p, a, b);
        float d = dist(c, p);
        if (d < bestD) bestD = d, best = c;
    });
    return best;
}

bool Pathfinder::lineOfSight(Vec2 a, Vec2 b) const {
    bool blocked = false;
    // Every boundary vertex lying on ab splits it; each piece must be inside.
    std::vector<float> ts{0.0f, 1.0f};
    Vec2 ab = b - a;
    float len2 = dot(ab, ab);
    forEachEdge(area_, [&](Vec2 c, Vec2 d) {
        if (blocked) return;
        if (segmentsCrossProperly(a, b, c, d)) {
            blocked = true;
            return;
        }
        if (len2 > 0 && dist(closestPointOnSegment(c, a, b), c) <= kGeomEps)
            ts.push_back(dot(c - a, ab) / len2);
    });
    if (blocked) return false;
    std::sort(ts.begin(), ts.end());
    for (size_t i = 0; i + 1 < ts.size(); ++i) {
        if (ts[i + 1] - ts[i] < 1e-6f) continue;
        if (!inside(a + ab * ((ts[i] + ts[i + 1]) * 0.5f))) return false;
    }
    return true;
}

std::vector<Vec2> Pathfinder::findPath(Vec2 from, Vec2 to) const {
    from = clampInside(from);
    to = clampInside(to);
    if (from == to) return {};
    if (lineOfSight(from, to)) return {to};

    // A* over: static nodes [0, n), start = n, goal = n + 1.
    const int n = int(nodes_.size()), start = n, goal = n + 1;
    auto pos = [&](int i) { return i == start ? from : i == goal ? to : nodes_[size_t(i)]; };
    std::vector<char> seesGoal(size_t(n), 0);
    for (int i = 0; i < n; ++i) seesGoal[size_t(i)] = lineOfSight(nodes_[size_t(i)], to);

    std::vector<float> g(size_t(n + 2), std::numeric_limits<float>::max());
    std::vector<int> parent(size_t(n + 2), -1);
    std::vector<char> closed(size_t(n + 2), 0);
    using QItem = std::pair<float, int>;
    std::priority_queue<QItem, std::vector<QItem>, std::greater<QItem>> open;
    g[size_t(start)] = 0;
    open.push({dist(from, to), start});

    auto relax = [&](int u, int v) {
        float ng = g[size_t(u)] + dist(pos(u), pos(v));
        if (ng < g[size_t(v)]) {
            g[size_t(v)] = ng;
            parent[size_t(v)] = u;
            open.push({ng + dist(pos(v), to), v});
        }
    };
    while (!open.empty()) {
        int u = open.top().second;
        open.pop();
        if (closed[size_t(u)]) continue;
        closed[size_t(u)] = 1;
        if (u == goal) break;
        if (u == start) {
            for (int v = 0; v < n; ++v)
                if (lineOfSight(from, nodes_[size_t(v)])) relax(u, v);
            continue;
        }
        for (int v : edges_[size_t(u)]) relax(u, v);
        if (seesGoal[size_t(u)]) relax(u, goal);
    }
    if (parent[size_t(goal)] < 0) return {};
    std::vector<Vec2> path;
    for (int v = goal; v != start; v = parent[size_t(v)]) path.push_back(pos(v));
    std::reverse(path.begin(), path.end());
    return path;
}

}  // namespace qe
