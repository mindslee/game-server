#pragma once
#include <string>
#include <vector>
#include <map>
#include <iostream>
#include <sol/sol.hpp>

class Player;
class Monster;

class LuaEngine {
public:
    LuaEngine()  = default;
    ~LuaEngine() = default;

    bool initialize(const std::string& scriptDir);
    bool loadScript(const std::string& moduleName);
    bool reloadScript(const std::string& moduleName);

    // C++ 객체를 Lua 쪽에 등록/해제 (persistent identity 보장)
    void registerPlayer(Player* player);
    void unregisterPlayer(Player* player);
    void registerMonster(Monster* monster);
    void unregisterMonster(Monster* monster);

    // Lua event_manager.publish 호출
    void publishEvent(Player* owner, const std::string& eventName);
    void publishEvent(Player* owner, const std::string& eventName, Monster* target);
    void publishEvent(Player* owner, const std::string& eventName, Player* target);

    // 직업 스탯 조회 (class_manager.getStats)
    struct ClassStats {
        int   maxHp;
        int   attackPower;
        float attackSpeed;
        float attackRange;
    };
    ClassStats getClassStats(Player* player);

    // formula.lua 공식 조회 (RELOAD formula 로 핫픽스 가능)
    struct CriticalInfo {
        int chance;      // 0~100 (%)
        int multiplier;  // 배율
    };
    CriticalInfo getCritical(Player* player);
    int          getMonsterDamage(Monster* monster);
    int          getExpReward(Player* player, Monster* monster);
    int          getExpToLevel(int level);

    struct DeathPenalty {
        int  resetLevel;   // -1 이면 레벨 유지
        int  resetExp;     // -1 이면 경험치 유지
    };
    DeathPenalty getDeathPenalty(Player* player);

    // P1: 스폰·리스폰·AI 설정 (RELOAD formula 핫픽스 가능)
    struct SpawnEntry {
        int         id;
        std::string name;
        int         level;
        float       x, y;
    };
    std::vector<SpawnEntry> getSpawnTable();

    struct RespawnConfig {
        int   minMs        = 4000;
        int   maxMs        = 7000;
        int   warningMs    = 1000;
        int   eliteChance  = 10;
        int   splitChance  = 20;
        int   maxMonsters  = 9;
        float homeRadius   = 20.0f;
    };
    RespawnConfig getRespawnConfig();

    int  getRespawnLevel(int baseLevel, int currentLevel);

    struct MonsterBaseStats {
        int maxHp;
        int coin;
    };
    MonsterBaseStats getMonsterBaseStats(int level, bool isElite);

    struct MonsterAI {
        float aggroSpeed  = 4.0f;
        float attackSpeed = 0.8f;
    };
    MonsterAI getMonsterAI(Monster* monster);

    // P2: 맵·리젠·틱 설정
    struct MapConfig {
        float width  = 100.0f;
        float height = 100.0f;
    };
    MapConfig getMapConfig();

    struct RegenConfig {
        float intervalSec = 5.0f;
    };
    RegenConfig getRegenConfig();

    struct TickConfig {
        int tickMs = 50;
    };
    TickConfig getTickConfig();

    // 각 매니저 프록시
    bool applyBuff(Player* owner, int buffId);
    bool removeBuff(Player* owner, int buffId);
    bool canUseItem(Player* owner, int itemId);
    void useItem(Player* owner, int itemId);
    bool acceptQuest(Player* owner, int questId);
    bool completeQuest(Player* owner, int questId);
    bool removeQuest(Player* owner, int questId);

private:
    sol::state lua;
    std::string scriptDir;

    // id → 영구 sol::object (event_manager 테이블 키 동일성 보장)
    std::map<int, sol::object> playerObjects;
    std::map<int, sol::object> monsterObjects;

    void setupPackagePath();
    void registerTypes();
    void registerQuestManagerExtensions();

    sol::object playerObj(Player* p);
    sol::object monsterObj(Monster* m);

    // 매니저 함수 호출 헬퍼: 에러 출력 후 result 반환
    template<typename... Args>
    sol::protected_function_result luaCall(
        const std::string& tbl, const std::string& fn, Args&&... args)
    {
        sol::protected_function f = lua[tbl][fn];
        auto res = f(std::forward<Args>(args)...);
        if (!res.valid()) {
            sol::error e = res;
            std::cerr << "[LuaEngine] " << tbl << "." << fn << ": " << e.what() << "\n";
        }
        return res;
    }
};
