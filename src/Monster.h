#pragma once
#include "Entity.h"
#include <chrono>
#include <string>
#include <map>

class Monster : public Entity {
public:
    Monster(int id, const std::string& name, int level = 1);

    void takeDamage(int dmg);

    // 리스폰
    std::chrono::steady_clock::time_point respawnAt;  // 기본값(zero) = 미예약
    std::string baseName;    // 엘리트 변이 전 원본 이름
    bool isElite     = false;
    bool warningSent = false; // 리스폰 예고 방송 여부
    int  baseCoin;            // 원래 코인 공식 값 (baseLevel * 10)
    int  baseLevel;           // 원래 레벨 (레벨 변동 기준)
    Vec2 homePos;             // 원래 스폰 구역 중심 (구역 귀환용)

    int   aggroTargetId = -1;   // 추격 대상 플레이어 ID (-1 = 없음)
    float aggroSpeed    = 4.0f; // 추격 시 이동 속도 (units/sec)

    // ── 공격 기여도 시스템 ───────────────────────────────────────────
    struct Contribution {
        int   damage  = 0;   // 누적 데미지 기여
        int   tanking = 0;   // 누적 탱킹(피격 흡수) 기여
        std::chrono::steady_clock::time_point lastTime;  // 마지막 기여 시각
    };
    std::map<int, Contribution> contributions;  // playerId → 기여 정보

    void addDamageContribution(int playerId, int damage);
    void addTankingContribution(int playerId, int damage);
    void expireContributions(float expireSec);  // 만료된 기여 제거
    void clearContributions();                  // 리스폰 시 초기화
};
