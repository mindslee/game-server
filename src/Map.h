#pragma once
#include "Vec2.h"

class Map {
public:
    static constexpr float WIDTH        = 100.f;
    static constexpr float HEIGHT       = 100.f;
    static constexpr float ATTACK_RANGE = 5.0f;

    static bool isInBounds(float x, float y) {
        return x >= 0.f && x < WIDTH && y >= 0.f && y < HEIGHT;
    }

    static bool inAttackRange(const Vec2& attacker, const Vec2& target) {
        return attacker.distanceTo(target) <= ATTACK_RANGE;
    }
};
