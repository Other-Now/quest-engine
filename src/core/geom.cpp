#include "core/geom.hpp"

namespace qe {

float signedArea(const Polygon& poly) {
    float a = 0;
    for (size_t i = 0, n = poly.size(); i < n; ++i) a += cross(poly[i], poly[(i + 1) % n]);
    return a * 0.5f;
}

Vec2 closestPointOnSegment(Vec2 p, Vec2 a, Vec2 b) {
    Vec2 ab = b - a;
    float len2 = dot(ab, ab);
    if (len2 <= 0) return a;
    float t = dot(p - a, ab) / len2;
    if (t < 0) t = 0;
    if (t > 1) t = 1;
    return a + ab * t;
}

bool onBoundary(const Polygon& poly, Vec2 p, float eps) {
    for (size_t i = 0, n = poly.size(); i < n; ++i)
        if (dist(closestPointOnSegment(p, poly[i], poly[(i + 1) % n]), p) <= eps) return true;
    return false;
}

bool pointInPolygon(const Polygon& poly, Vec2 p) {
    if (onBoundary(poly, p)) return true;
    bool in = false;
    for (size_t i = 0, n = poly.size(), j = n - 1; i < n; j = i++) {
        Vec2 a = poly[i], b = poly[j];
        if ((a.y > p.y) != (b.y > p.y)) {
            float x = a.x + (p.y - a.y) * (b.x - a.x) / (b.y - a.y);
            if (p.x < x) in = !in;
        }
    }
    return in;
}

static int orient(Vec2 a, Vec2 b, Vec2 c) {
    float v = cross(b - a, c - a);
    if (v > kGeomEps) return 1;
    if (v < -kGeomEps) return -1;
    return 0;
}

bool segmentsCrossProperly(Vec2 a, Vec2 b, Vec2 c, Vec2 d) {
    int o1 = orient(a, b, c), o2 = orient(a, b, d);
    int o3 = orient(c, d, a), o4 = orient(c, d, b);
    return o1 * o2 < 0 && o3 * o4 < 0;
}

}  // namespace qe
