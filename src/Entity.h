#pragma once
#include <string>
#include <chrono>
#include "Vec2.h"

// ── 공통 베이스 클래스 ───────────────────────────────────────────────
// Player, Monster 가 공유하는 필드와 isAlive() 를 담는다.
// virtual 함수는 소멸자 하나만 두어 불필요한 vtable 오버헤드를 최소화한다.
class Entity {
public:
    int         id;
    std::string name;
    std::string type;   // "Player" | "Monster"
    int         level;
    int         hp;
    int         maxHp;
    int         coin;
    Vec2        pos;        // 현재 위치 (tick마다 갱신)
    Vec2        targetPos;  // 이동 목표 위치
    float       speed;      // 초당 이동 거리 (units/sec)

    float       attackRange = 5.0f;   // 공격 사거리 (units)
    float       attackSpeed = 1.0f;   // 초당 공격 횟수
    int         attackPower = 10;     // 공격력 (레벨에 따라 증가)
    int         hpRegen     = 0;      // HP 회복량 (속성값, 5초 주기)
    std::chrono::steady_clock::time_point lastAttackTime;  // 마지막 공격 시각 (쿨다운 계산)

    Entity(int id, const std::string& name, const std::string& type,
           int level, int hp, int maxHp, int coin = 0, float speed = 0.f);

    bool isAlive() const { return hp > 0; }

    // targetPos 방향으로 dt초 만큼 이동. 실제 이동했으면 true 반환.
    bool stepTowardTarget(float dt);

    virtual ~Entity() = default;
};
