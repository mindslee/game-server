#include "Monster.h"
#include <algorithm>
#include <iostream>

Monster::Monster(int id, const std::string& name, int level)
    : Entity(id, name, "Monster", level, 100, 100, level * 10, 0.f)  // speed = 0 (정적)
    , baseName(name)
    , baseCoin(level * 10)
    , baseLevel(level)
{
    attackSpeed = 0.8f;  // 기본 공격속도: 초당 0.8회
}

void Monster::takeDamage(int dmg) {
    hp = std::max(0, hp - dmg);
    std::cout << "[Monster:" << name << "] took " << dmg
              << " dmg, hp=" << hp << "\n";
}

void Monster::addDamageContribution(int playerId, int damage) {
    auto& c = contributions[playerId];
    c.damage += damage;
    c.lastTime = std::chrono::steady_clock::now();
}

void Monster::addTankingContribution(int playerId, int damage) {
    auto& c = contributions[playerId];
    c.tanking += damage;
    c.lastTime = std::chrono::steady_clock::now();
}

void Monster::expireContributions(float expireSec) {
    auto now = std::chrono::steady_clock::now();
    for (auto it = contributions.begin(); it != contributions.end(); ) {
        float elapsed = std::chrono::duration<float>(now - it->second.lastTime).count();
        if (elapsed >= expireSec) {
            it = contributions.erase(it);
        } else {
            ++it;
        }
    }
}

void Monster::clearContributions() {
    contributions.clear();
}
