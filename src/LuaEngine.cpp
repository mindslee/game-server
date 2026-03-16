#include "LuaEngine.h"
#include "Player.h"
#include "Monster.h"
#include <iostream>

bool LuaEngine::initialize(const std::string& dir) {
    scriptDir = dir;
    lua.open_libraries(sol::lib::base, sol::lib::package, sol::lib::string,
                       sol::lib::math,  sol::lib::table,  sol::lib::io);
    registerTypes();
    setupPackagePath();
    for (const char* m : {"event_manager", "item_manager",
                           "buff_manager",  "quest_manager", "monster_manager",
                           "player_manager", "class_manager", "formula"}) {
        if (!loadScript(m)) {
            std::cerr << "[LuaEngine] Failed to load " << m << "\n";
            return false;
        }
    }
    registerQuestManagerExtensions();
    for (const char* c : {"item.1000", "buff.1000", "quest.1000", "player.base"})
        loadScript(c);
    std::cout << "[LuaEngine] Initialized. Script dir: " << scriptDir << "\n";
    return true;
}

void LuaEngine::registerTypes() {
    lua.new_usertype<Player>("Player",
        sol::meta_function::to_string, [](const Player& p) {
            return "Player(" + p.name + ", lv=" + std::to_string(p.level) + ")";
        },
        "id",   sol::readonly_property([](const Player& p) { return p.id;   }),
        "name", sol::readonly_property([](const Player& p) { return p.name; }),
        "type", sol::property([](const Player&) -> std::string { return "Player"; }),
        "level",  sol::property([](const Player& p) { return p.level;  },
                                [](Player& p, int v)  { p.level  = v;  }),
        "gender", sol::property([](const Player& p) { return p.gender; },
                                [](Player& p, int v)  { p.gender = v;  }),
        "coin",   sol::property([](const Player& p) { return p.coin;   },
                                [](Player& p, int v)  { p.coin   = v;  }),
        "hp",     sol::property([](const Player& p) { return p.hp;     },
                                [](Player& p, int v)  { p.hp     = v;  }),
        "addCoin",         &Player::addCoin,
        "AddHP",           &Player::addHP,
        "setQuestData",    &Player::setQuestData,
        "addQuestData",    &Player::addQuestData,
        "removeQuestData", &Player::removeQuestData,
        "getCustomData", [](Player& p, const std::string& key, sol::this_state s) -> sol::table {
            sol::state_view lv(s);
            std::string regKey = "__custom:" + std::to_string(p.id) + ":" + key;
            sol::object existing = lv.registry()[regKey];
            if (!existing.valid() || existing.get_type() != sol::type::table) {
                sol::table t = lv.create_table_with("count", 0);
                lv.registry()[regKey] = t;
                return t;
            }
            return existing.as<sol::table>();
        }
    );

    lua.new_usertype<Monster>("Monster",
        sol::meta_function::to_string, [](const Monster& m) {
            return "Monster(" + m.name + ", lv=" + std::to_string(m.level) + ")";
        },
        "id",    sol::readonly_property([](const Monster& m) { return m.id;    }),
        "name",  sol::readonly_property([](const Monster& m) { return m.name;  }),
        "type",  sol::readonly_property([](const Monster& m) { return m.type;  }),
        "level", sol::readonly_property([](const Monster& m) { return m.level; }),
        "hp",    sol::readonly_property([](const Monster& m) { return m.hp;    }),
        "maxHp", sol::readonly_property([](const Monster& m) { return m.maxHp; }),
        "coin",  sol::readonly_property([](const Monster& m) { return m.coin;  })
    );
}

void LuaEngine::setupPackagePath() {
    std::string path = scriptDir + "/?.lua;" + scriptDir + "/?/init.lua";
    lua["package"]["path"] = path;
}

void LuaEngine::registerQuestManagerExtensions() {
    sol::optional<sol::table> qm = lua["quest_manager"];
    if (!qm) {
        std::cerr << "[LuaEngine] Warning: quest_manager not found\n";
        return;
    }
    (*qm)["getQuestTable"] = [this](int questId) -> sol::table {
        sol::table t = lua.create_table();
        if (questId == 1000) {
            t["kill_target_id"]   = 2000;
            t["kill_target_type"] = "Monster";
            t["kill_count"]       = 5;
            t["reward_coin"]      = 500;
        }
        return t;
    };
    std::cout << "[LuaEngine] quest_manager.getQuestTable registered\n";
}

bool LuaEngine::loadScript(const std::string& moduleName) {
    auto res = lua.safe_script(
        "require('" + moduleName + "')",
        sol::script_pass_on_error
    );
    if (!res.valid()) {
        sol::error e = res;
        std::cerr << "[LuaEngine] require('" << moduleName << "'): " << e.what() << "\n";
        return false;
    }
    return true;
}

bool LuaEngine::reloadScript(const std::string& moduleName) {
    lua["package"]["loaded"][moduleName] = sol::lua_nil;
    std::cout << "[LuaEngine] Reloading: " << moduleName << "\n";
    return loadScript(moduleName);
}

void LuaEngine::registerPlayer(Player* player) {
    if (playerObjects.count(player->id)) return;
    playerObjects[player->id] = sol::make_object(lua, player);
    std::cout << "[LuaEngine] Player " << player->name << " registered\n";
    // 기본 플레이어 스크립트 이벤트 구독
    luaCall("player_manager", "initPlayer", playerObj(player));
}
void LuaEngine::unregisterPlayer(Player* player) { playerObjects.erase(player->id); }

void LuaEngine::registerMonster(Monster* monster) {
    if (monsterObjects.count(monster->id)) return;
    monsterObjects[monster->id] = sol::make_object(lua, monster);
    std::cout << "[LuaEngine] Monster " << monster->name << " registered\n";
}
void LuaEngine::unregisterMonster(Monster* monster) { monsterObjects.erase(monster->id); }

sol::object LuaEngine::playerObj(Player* p) {
    auto it = playerObjects.find(p->id);
    return it != playerObjects.end() ? it->second : sol::lua_nil;
}
sol::object LuaEngine::monsterObj(Monster* m) {
    auto it = monsterObjects.find(m->id);
    return it != monsterObjects.end() ? it->second : sol::lua_nil;
}

void LuaEngine::publishEvent(Player* owner, const std::string& ev) {
    luaCall("event_manager", "publish", playerObj(owner), ev);
}
void LuaEngine::publishEvent(Player* owner, const std::string& ev, Monster* target) {
    luaCall("event_manager", "publish", playerObj(owner), ev, monsterObj(target));
}
void LuaEngine::publishEvent(Player* owner, const std::string& ev, Player* target) {
    luaCall("event_manager", "publish", playerObj(owner), ev, playerObj(target));
}

static bool boolResult(sol::protected_function_result& res, bool def) {
    if (!res.valid()) return def;
    sol::optional<bool> v = res;
    return v.value_or(def);
}

LuaEngine::ClassStats LuaEngine::getClassStats(Player* p) {
    ClassStats def{ 100, 10, 1.0f, 5.0f };
    sol::protected_function f = lua["class_manager"]["getStats"];
    if (!f.valid()) return def;
    auto res = f(p->job, p->level);
    if (!res.valid()) {
        sol::error e = res;
        std::cerr << "[LuaEngine] class_manager.getStats: " << e.what() << "\n";
        return def;
    }
    sol::table t = res;
    def.maxHp       = t.get_or("maxHp",       def.maxHp);
    def.attackPower = t.get_or("attackPower",  def.attackPower);
    def.attackSpeed = t.get_or<float>("attackSpeed", def.attackSpeed);
    def.attackRange = t.get_or<float>("attackRange", def.attackRange);
    return def;
}

// ================================================================
// formula.lua 공식 조회
// ================================================================

LuaEngine::CriticalInfo LuaEngine::getCritical(Player* p) {
    CriticalInfo def{ 15, 2 };
    sol::protected_function f = lua["formula"]["getCritical"];
    if (!f.valid()) return def;
    auto res = f(playerObj(p));
    if (!res.valid()) {
        sol::error e = res;
        std::cerr << "[LuaEngine] formula.getCritical: " << e.what() << "\n";
        return def;
    }
    sol::table t = res;
    def.chance     = t.get_or("chance",     def.chance);
    def.multiplier = t.get_or("multiplier", def.multiplier);
    return def;
}

int LuaEngine::getMonsterDamage(Monster* m) {
    sol::protected_function f = lua["formula"]["monsterDamage"];
    if (!f.valid()) return std::max(1, m->level * 5);  // fallback
    auto res = f(monsterObj(m));
    if (!res.valid()) {
        sol::error e = res;
        std::cerr << "[LuaEngine] formula.monsterDamage: " << e.what() << "\n";
        return std::max(1, m->level * 5);
    }
    return res.get<int>();
}

int LuaEngine::getExpReward(Player* p, Monster* m) {
    sol::protected_function f = lua["formula"]["expReward"];
    if (!f.valid()) return m->maxHp;  // fallback
    auto res = f(playerObj(p), monsterObj(m));
    if (!res.valid()) {
        sol::error e = res;
        std::cerr << "[LuaEngine] formula.expReward: " << e.what() << "\n";
        return m->maxHp;
    }
    return res.get<int>();
}

int LuaEngine::getExpToLevel(int level) {
    sol::protected_function f = lua["formula"]["expToLevel"];
    if (!f.valid()) return level * 100;  // fallback
    auto res = f(level);
    if (!res.valid()) {
        sol::error e = res;
        std::cerr << "[LuaEngine] formula.expToLevel: " << e.what() << "\n";
        return level * 100;
    }
    return res.get<int>();
}

LuaEngine::DeathPenalty LuaEngine::getDeathPenalty(Player* p) {
    DeathPenalty def{ 1, 0 };
    sol::protected_function f = lua["formula"]["deathPenalty"];
    if (!f.valid()) return def;
    auto res = f(playerObj(p));
    if (!res.valid()) {
        sol::error e = res;
        std::cerr << "[LuaEngine] formula.deathPenalty: " << e.what() << "\n";
        return def;
    }
    sol::table t = res;
    def.resetLevel = t.get_or("resetLevel", def.resetLevel);
    def.resetExp   = t.get_or("resetExp",   def.resetExp);
    return def;
}

// ================================================================
// P1: 스폰·리스폰·AI 설정
// ================================================================

std::vector<LuaEngine::SpawnEntry> LuaEngine::getSpawnTable() {
    std::vector<SpawnEntry> result;
    sol::protected_function f = lua["formula"]["getSpawnTable"];
    if (!f.valid()) return result;
    auto res = f();
    if (!res.valid()) {
        sol::error e = res;
        std::cerr << "[LuaEngine] formula.getSpawnTable: " << e.what() << "\n";
        return result;
    }
    sol::table arr = res;
    for (size_t i = 1; i <= arr.size(); ++i) {
        sol::table entry = arr[i];
        SpawnEntry se;
        se.id    = entry.get_or("id",    2000 + (int)i);
        se.name  = entry.get_or<std::string>("name", "Unknown");
        se.level = entry.get_or("level", 1);
        se.x     = entry["x"].get_or(-1.0f);
        se.y     = entry["y"].get_or(-1.0f);
        result.push_back(std::move(se));
    }
    return result;
}

LuaEngine::RespawnConfig LuaEngine::getRespawnConfig() {
    RespawnConfig def;
    sol::protected_function f = lua["formula"]["getRespawnConfig"];
    if (!f.valid()) return def;
    auto res = f();
    if (!res.valid()) {
        sol::error e = res;
        std::cerr << "[LuaEngine] formula.getRespawnConfig: " << e.what() << "\n";
        return def;
    }
    sol::table t = res;
    def.minMs       = t.get_or("minMs",       def.minMs);
    def.maxMs       = t.get_or("maxMs",       def.maxMs);
    def.warningMs   = t.get_or("warningMs",   def.warningMs);
    def.eliteChance = t.get_or("eliteChance", def.eliteChance);
    def.splitChance = t.get_or("splitChance", def.splitChance);
    def.maxMonsters = t.get_or("maxMonsters", def.maxMonsters);
    def.homeRadius  = t.get_or<float>("homeRadius", def.homeRadius);
    return def;
}

int LuaEngine::getRespawnLevel(int baseLevel, int currentLevel) {
    sol::protected_function f = lua["formula"]["respawnLevel"];
    if (!f.valid()) return currentLevel;
    auto res = f(baseLevel, currentLevel);
    if (!res.valid()) {
        sol::error e = res;
        std::cerr << "[LuaEngine] formula.respawnLevel: " << e.what() << "\n";
        return currentLevel;
    }
    return res.get<int>();
}

LuaEngine::MonsterBaseStats LuaEngine::getMonsterBaseStats(int level, bool isElite) {
    MonsterBaseStats def{ 100 + (level - 1) * 20, level * 10 };
    sol::protected_function f = lua["formula"]["monsterBaseStats"];
    if (!f.valid()) return def;
    auto res = f(level, isElite);
    if (!res.valid()) {
        sol::error e = res;
        std::cerr << "[LuaEngine] formula.monsterBaseStats: " << e.what() << "\n";
        return def;
    }
    sol::table t = res;
    def.maxHp = t.get_or("maxHp", def.maxHp);
    def.coin  = t.get_or("coin",  def.coin);
    return def;
}

LuaEngine::MonsterAI LuaEngine::getMonsterAI(Monster* m) {
    MonsterAI def;
    sol::protected_function f = lua["formula"]["monsterAI"];
    if (!f.valid()) return def;
    auto res = f(monsterObj(m));
    if (!res.valid()) {
        sol::error e = res;
        std::cerr << "[LuaEngine] formula.monsterAI: " << e.what() << "\n";
        return def;
    }
    sol::table t = res;
    def.aggroSpeed  = t.get_or<float>("aggroSpeed",  def.aggroSpeed);
    def.attackSpeed = t.get_or<float>("attackSpeed",  def.attackSpeed);
    return def;
}

// ================================================================
// P2: 맵·리젠·틱 설정
// ================================================================

LuaEngine::MapConfig LuaEngine::getMapConfig() {
    MapConfig def;
    sol::protected_function f = lua["formula"]["getMapConfig"];
    if (!f.valid()) return def;
    auto res = f();
    if (!res.valid()) {
        sol::error e = res;
        std::cerr << "[LuaEngine] formula.getMapConfig: " << e.what() << "\n";
        return def;
    }
    sol::table t = res;
    def.width  = t.get_or<float>("width",  def.width);
    def.height = t.get_or<float>("height", def.height);
    return def;
}

LuaEngine::RegenConfig LuaEngine::getRegenConfig() {
    RegenConfig def;
    sol::protected_function f = lua["formula"]["getRegenConfig"];
    if (!f.valid()) return def;
    auto res = f();
    if (!res.valid()) {
        sol::error e = res;
        std::cerr << "[LuaEngine] formula.getRegenConfig: " << e.what() << "\n";
        return def;
    }
    sol::table t = res;
    def.intervalSec = t.get_or<float>("intervalSec", def.intervalSec);
    return def;
}

LuaEngine::TickConfig LuaEngine::getTickConfig() {
    TickConfig def;
    sol::protected_function f = lua["formula"]["getTickConfig"];
    if (!f.valid()) return def;
    auto res = f();
    if (!res.valid()) {
        sol::error e = res;
        std::cerr << "[LuaEngine] formula.getTickConfig: " << e.what() << "\n";
        return def;
    }
    sol::table t = res;
    def.tickMs = t.get_or("tickMs", def.tickMs);
    return def;
}

bool LuaEngine::applyBuff(Player* o, int id) {
    auto res = luaCall("buff_manager",  "applyBuff",     playerObj(o), id);
    return boolResult(res, false);
}
bool LuaEngine::removeBuff(Player* o, int id) {
    auto res = luaCall("buff_manager",  "removeBuff",    playerObj(o), id);
    return boolResult(res, false);
}
bool LuaEngine::canUseItem(Player* o, int id) {
    auto res = luaCall("item_manager",  "canUse",        playerObj(o), id);
    return boolResult(res, true);
}
void LuaEngine::useItem(Player* o, int id) {
    luaCall("item_manager",  "use", playerObj(o), id);
}
bool LuaEngine::acceptQuest(Player* o, int id) {
    auto res = luaCall("quest_manager", "acceptQuest",   playerObj(o), id);
    return boolResult(res, false);
}
bool LuaEngine::completeQuest(Player* o, int id) {
    auto res = luaCall("quest_manager", "completeQuest", playerObj(o), id);
    return boolResult(res, false);
}
bool LuaEngine::removeQuest(Player* o, int id) {
    auto res = luaCall("quest_manager", "removeQuest",   playerObj(o), id);
    return boolResult(res, false);
}
