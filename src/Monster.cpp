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
