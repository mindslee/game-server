#pragma once
#include <cmath>
#include <string>

struct Vec2 {
    float x = 0.f;
    float y = 0.f;

    float distanceTo(const Vec2& o) const {
        float dx = x - o.x, dy = y - o.y;
        return std::sqrtf(dx * dx + dy * dy);
    }

    std::string toString() const {
        char buf[32];
        std::snprintf(buf, sizeof(buf), "(%.1f, %.1f)", x, y);
        return buf;
    }
};
