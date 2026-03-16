#include "Entity.h"
#include <cmath>

Entity::Entity(int id, const std::string& name, const std::string& type,
               int level, int hp, int maxHp, int coin, float speed)
    : id(id), name(name), type(type), level(level)
    , hp(hp), maxHp(maxHp), coin(coin), speed(speed) {}

bool Entity::stepTowardTarget(float dt) {
    float dx   = targetPos.x - pos.x;
    float dy   = targetPos.y - pos.y;
    float dist = std::sqrtf(dx * dx + dy * dy);
    if (dist < 0.001f) return false;          // 이미 목표에 도달

    float move = speed * dt;
    if (move >= dist) {
        pos = targetPos;                      // 한 틱에 도달
    } else {
        float inv = move / dist;
        pos.x += dx * inv;
        pos.y += dy * inv;
    }
    return true;
}
