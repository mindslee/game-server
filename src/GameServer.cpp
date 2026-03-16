#include "GameServer.h"
#include "WsServer.h"
#include "LuaEngine.h"
#include "Player.h"
#include "Monster.h"

#include <nlohmann/json.hpp>
#include <iostream>
#include <sstream>
#include <algorithm>
#include <random>
#include <cstdio>
#include <cmath>

// ── 리스폰 튜닝 상수 → Lua formula.lua 로 이관 (RELOAD formula 핫픽스 가능) ──
// 아래 상수들은 fallback으로만 남김 — 실제 값은 lua_->getRespawnConfig() 에서 조회
static constexpr float PI_F = 3.14159265f;

using json = nlohmann::json;

// ── 직업별 스탯 적용 ──────────────────────────────────────────────
//   레벨업/리스폰/생성 시 호출하여 HP·공격 속성을 재계산한다.
//   스탯값은 lua/EventManager/class_manager.lua 에서 관리 (RELOAD 핫픽스 가능).
static void applyClassStats(Player* p, LuaEngine& lua) {
    auto s        = lua.getClassStats(p);
    p->maxHp       = s.maxHp;
    p->attackPower = s.attackPower;
    p->attackSpeed = s.attackSpeed;
    p->attackRange = s.attackRange;
}

// Random position within map bounds
static Vec2 randomPos(float mapW = Map::WIDTH, float mapH = Map::HEIGHT) {
    static std::mt19937 rng(std::random_device{}());
    std::uniform_real_distribution<float> distW(0.f, mapW - 0.001f);
    std::uniform_real_distribution<float> distH(0.f, mapH - 0.001f);
    return { distW(rng), distH(rng) };
}

// ================================================================
// Session
// ================================================================

std::atomic<int> Session::nextId_{100};

Session::Session(asio::ip::tcp::socket socket, GameServer& server)
    : socket_(std::move(socket))
    , server_(server)
    , playerId_(nextId_.fetch_add(1))
{}

void Session::start() {
    doWrite("GameServer v1.0 -- type CONNECT <name> to begin");
    doRead();
}

void Session::doRead() {
    auto self = shared_from_this();
    asio::async_read_until(socket_, readBuf_, '\n',
        [this, self](std::error_code ec, std::size_t /*bytes*/) {
            if (ec) {
                std::cout << "[Session] Disconnected (playerId=" << playerId_ << ")\n";
                return;
            }

            std::istream is(&readBuf_);
            std::string line;
            std::getline(is, line);
            if (!line.empty() && line.back() == '\r') line.pop_back();

            if (!line.empty()) {
                std::string resp = server_.processCommand(playerId_, line);
                doWrite(resp);

                std::string upper = line;
                std::transform(upper.begin(), upper.end(), upper.begin(), ::toupper);
                if (upper == "QUIT" || upper == "EXIT") return; // session ends
            }

            doRead();
        });
}

void Session::doWrite(const std::string& msg) {
    writeQueue_.push_back(msg + "\r\n");
    if (!writing_) flushWrite();
}

void Session::flushWrite() {
    if (writeQueue_.empty()) { writing_ = false; return; }
    writing_ = true;
    auto self = shared_from_this();
    // Buffer points into writeQueue_.front() — valid until pop_front() in handler
    asio::async_write(socket_, asio::buffer(writeQueue_.front()),
        [this, self](std::error_code ec, std::size_t /*bytes*/) {
            writeQueue_.pop_front();
            if (!ec) flushWrite();
            else     writing_ = false;
        });
}

// ================================================================
// GameServer — construction / init
// ================================================================

GameServer::GameServer()
    : ioc_()
    , acceptor_(ioc_)
    , tickTimer_(ioc_)
{}

GameServer::~GameServer() { stop(); }

bool GameServer::initialize(const std::string& scriptDir, int p) {
    port_ = p;
    lua_ = std::make_unique<LuaEngine>();
    if (!lua_->initialize(scriptDir)) {
        std::cerr << "[GameServer] Lua initialization failed\n";
        return false;
    }
    std::cout << "[GameServer] Initialized on port " << port_ << "\n";
    return true;
}

// ================================================================
// Object management
// ================================================================

Player* GameServer::createPlayer(int id, const std::string& name, int level, int gender,
                                  const std::string& job) {
    auto p   = std::make_unique<Player>(id, name, level, gender);
    p->job   = job;
    applyClassStats(p.get(), *lua_);    // 직업별 maxHp / attackSpeed / attackRange / attackPower 설정
    p->hp        = p->maxHp;    // 생성 시 HP 만땅
    auto mc = lua_->getMapConfig();
    p->pos       = randomPos(mc.width, mc.height);
    p->targetPos = p->pos;
    Player* ptr  = p.get();
    players_[id] = std::move(p);
    lua_->registerPlayer(ptr);
    std::cout << "[GameServer] Player '" << name << "' (" << job
              << " lv" << level << ") spawned at " << ptr->pos.toString() << "\n";
    return ptr;
}

Monster* GameServer::createMonster(int id, const std::string& name, int level, float x, float y) {
    auto m        = std::make_unique<Monster>(id, name, level);
    auto mcm = lua_->getMapConfig();
    m->pos        = (x >= 0.f && y >= 0.f) ? Vec2{x, y} : randomPos(mcm.width, mcm.height);
    m->targetPos  = m->pos;
    m->homePos    = m->pos;   // 구역 귀환 기준점
    Monster* ptr = m.get();
    monsters_[id] = std::move(m);
    lua_->registerMonster(ptr);
    std::cout << "[GameServer] Monster '" << name << "' placed at " << ptr->pos.toString() << "\n";
    return ptr;
}

Player* GameServer::getPlayer(int id) {
    auto it = players_.find(id);
    return it != players_.end() ? it->second.get() : nullptr;
}

Monster* GameServer::getMonster(int id) {
    auto it = monsters_.find(id);
    return it != monsters_.end() ? it->second.get() : nullptr;
}

void GameServer::spawnMonstersFromLua() {
    std::lock_guard<std::recursive_mutex> lock(gameMutex_);
    auto table = lua_->getSpawnTable();
    for (auto& entry : table) {
        auto* m = createMonster(entry.id, entry.name, entry.level, entry.x, entry.y);
        // Lua AI 파라미터 적용
        auto ai = lua_->getMonsterAI(m);
        m->aggroSpeed  = ai.aggroSpeed;
        m->attackSpeed = ai.attackSpeed;
        // Lua 스탯 적용
        auto stats = lua_->getMonsterBaseStats(entry.level, false);
        m->maxHp = stats.maxHp;
        m->hp    = m->maxHp;
        m->coin  = stats.coin;
    }
    std::cout << "[GameServer] Spawned " << table.size() << " monsters from Lua spawn table\n";
}

void GameServer::removePlayer(int id) {
    std::lock_guard<std::recursive_mutex> lock(gameMutex_);
    auto it = players_.find(id);
    if (it == players_.end()) return;
    lua_->unregisterPlayer(it->second.get());
    players_.erase(it);
    std::cout << "[GameServer] Player " << id << " removed\n";
}

void GameServer::broadcastToOthers(int excludePlayerId) {
    if (!wsServer_) return;
    wsServer_->broadcastExcept(excludePlayerId, [this](int pid) {
        return getStateJson(pid);
    });
}

// ================================================================
// Game actions
// ================================================================

bool GameServer::playerAttack(Player* player, Monster* monster, int damage) {
    std::lock_guard<std::recursive_mutex> lock(gameMutex_);

    if (!monster->isAlive()) return false;

    // 사거리 체크 (플레이어 개인 attackRange 사용)
    float dist = player->pos.distanceTo(monster->pos);
    if (dist > player->attackRange) {
        std::cout << "[GameServer] " << player->name << " too far from " << monster->name
                  << " (dist=" << dist << ", range=" << player->attackRange << ")\n";
        return false;
    }

    // 쿨다운 체크 (서버 side anti-cheat)
    using tp = std::chrono::steady_clock::time_point;
    auto now = std::chrono::steady_clock::now();
    if (player->lastAttackTime != tp{}) {
        float elapsed = std::chrono::duration<float>(now - player->lastAttackTime).count();
        if (elapsed < 1.0f / player->attackSpeed) return false;
    }
    player->lastAttackTime = now;

    // 크리티컬 히트 (Lua formula.getCritical 에서 확률/배율 조회 — RELOAD formula 핫픽스 가능)
    // 클라이언트가 보낸 damage는 무시하고 서버 속성값 attackPower 사용
    static std::mt19937 critRng(std::random_device{}());
    static std::uniform_int_distribution<int> pct100(0, 99);
    auto crit = lua_->getCritical(player);
    bool isCrit       = (pct100(critRng) < crit.chance);
    int  actualDamage = isCrit ? player->attackPower * crit.multiplier : player->attackPower;

    monster->takeDamage(actualDamage);
    monster->aggroTargetId = player->id;  // 어그로 설정

    lua_->publishEvent(player, "onAttack", monster);
    std::cout << "[GameServer] " << player->name << " attacked " << monster->name
              << " (dist=" << dist << ", dmg=" << actualDamage
              << (isCrit ? " [CRIT]" : "") << ")\n";

    // attack_result 이벤트 브로드캐스트 (모든 클라이언트에게)
    auto event = json{
        {"type",       "attack_result"},
        {"attackerId", player->id},
        {"targetId",   monster->id},
        {"damage",     actualDamage},
        {"critical",   isCrit},
        {"hp",         monster->hp},
        {"maxHp",      monster->maxHp},
        {"killed",     !monster->isAlive()}
    }.dump();
    broadcastEvent(event);

    if (!monster->isAlive()) {
        lua_->publishEvent(player, "onKill", monster);
        std::cout << "[GameServer] " << player->name << " killed " << monster->name << "\n";
        monster->aggroTargetId = -1;
        monster->speed = 0.f;
        scheduleRespawn(monster);

        // ── 경험치 획득 (Lua formula.expReward — RELOAD formula 핫픽스 가능) ──
        int expGain = lua_->getExpReward(player, monster);
        player->exp += expGain;
        std::cout << "[GameServer] Player '" << player->name
                  << "' gained " << expGain << " EXP (total=" << player->exp << ")\n";

        // ── 레벨업 체크 (Lua formula.expToLevel — RELOAD formula 핫픽스 가능) ──
        int needed = lua_->getExpToLevel(player->level);
        while (player->exp >= needed) {
            player->exp -= needed;
            player->level++;
            applyClassStats(player, *lua_);    // 직업별 maxHp·attackPower 재계산
            player->hp = player->maxHp;        // 레벨업 시 체력 완전 회복
            std::cout << "[GameServer] Player '" << player->name
                      << "' leveled up to " << player->level
                      << "! maxHp=" << player->maxHp << "\n";
            auto lvEvent = json{
                {"type",     "player_levelup"},
                {"playerId", player->id},
                {"level",    player->level},
                {"maxHp",    player->maxHp}
            }.dump();
            broadcastEvent(lvEvent);
            needed = lua_->getExpToLevel(player->level);  // 다음 레벨 요구량 갱신
        }
    }
    return true;
}

void GameServer::playerStruck(Player* player, Monster* attacker) {
    std::lock_guard<std::recursive_mutex> lock(gameMutex_);
    lua_->publishEvent(player, "onStruck", attacker);
    std::cout << "[GameServer] " << player->name << " was struck by " << attacker->name << "\n";
}

void GameServer::playerKillMonster(Player* player, Monster* monster) {
    std::lock_guard<std::recursive_mutex> lock(gameMutex_);
    lua_->publishEvent(player, "onKill", monster);
    std::cout << "[GameServer] " << player->name << " killed " << monster->name << "\n";
    // 리스폰 예약 (직접 호출로 죽인 경우에도 리스폰 보장)
    if (!monster->isAlive() && monster->respawnAt == std::chrono::steady_clock::time_point{}) {
        monster->aggroTargetId = -1;
        monster->speed = 0.f;
        scheduleRespawn(monster);
    }
}

void GameServer::playerApplyBuff(Player* player, int buffId) {
    std::lock_guard<std::recursive_mutex> lock(gameMutex_);
    bool ok = lua_->applyBuff(player, buffId);
    std::cout << "[GameServer] Buff " << buffId << (ok ? " applied" : " failed") << " on " << player->name << "\n";
}

void GameServer::playerRemoveBuff(Player* player, int buffId) {
    std::lock_guard<std::recursive_mutex> lock(gameMutex_);
    lua_->removeBuff(player, buffId);
    std::cout << "[GameServer] Buff " << buffId << " removed from " << player->name << "\n";
}

void GameServer::playerUseItem(Player* player, int itemId) {
    std::lock_guard<std::recursive_mutex> lock(gameMutex_);
    if (lua_->canUseItem(player, itemId)) {
        lua_->useItem(player, itemId);
    } else {
        std::cout << "[GameServer] " << player->name << " cannot use item " << itemId << "\n";
    }
}

void GameServer::playerAcceptQuest(Player* player, int questId) {
    std::lock_guard<std::recursive_mutex> lock(gameMutex_);
    lua_->acceptQuest(player, questId);
    std::cout << "[GameServer] " << player->name << " accepted quest " << questId << "\n";
}

void GameServer::playerCompleteQuest(Player* player, int questId) {
    std::lock_guard<std::recursive_mutex> lock(gameMutex_);
    lua_->completeQuest(player, questId);
    std::cout << "[GameServer] " << player->name << " completed quest " << questId << "\n";
}

void GameServer::playerRemoveQuest(Player* player, int questId) {
    std::lock_guard<std::recursive_mutex> lock(gameMutex_);
    lua_->removeQuest(player, questId);
}

// ================================================================
// Test scenario
// ================================================================

void GameServer::runTestScenario() {
    std::cout << "\n=== Running test scenario ===\n\n";

    Player*  p1 = createPlayer(1, "player1", 10, 0);
    // Reuse existing monster if already created (e.g. by main), else create one
    Monster* m1 = getMonster(2000);
    if (!m1) m1 = createMonster(2000, "Goblin", 5, 20.f, 20.f);

    p1->printStatus();

    std::cout << "\n--- Apply buff 1000 ---\n";
    playerApplyBuff(p1, 1000);

    std::cout << "\n--- Use item 1000 ---\n";
    playerUseItem(p1, 1000);

    std::cout << "\n--- Accept quest 1000 ---\n";
    playerAcceptQuest(p1, 1000);

    std::cout << "\n--- Move player next to monster ---\n";
    p1->pos = { m1->pos.x + 1.0f, m1->pos.y };  // 1.0 units away (within range 2.0)

    std::cout << "\n--- Attack monster (triggers onAttack) ---\n";
    playerAttack(p1, m1, 30);

    std::cout << "\n--- Player struck by monster (triggers onStruck) ---\n";
    playerStruck(p1, m1);

    std::cout << "\n--- Kill monster (triggers onKill) ---\n";
    m1->takeDamage(9999);
    playerKillMonster(p1, m1);

    std::cout << "\n--- Complete quest 1000 ---\n";
    playerCompleteQuest(p1, 1000);

    std::cout << "\n--- Remove buff 1000 ---\n";
    playerRemoveBuff(p1, 1000);

    std::cout << "\n--- Reload buff.1000 script (hot reload) ---\n";
    {
        std::lock_guard<std::recursive_mutex> lock(gameMutex_);
        lua_->reloadScript("buff.1000");
    }

    std::cout << "\n";
    p1->printStatus();
    std::cout << "\n=== Test scenario complete ===\n\n";
}

// ================================================================
// Network (async)
// ================================================================

void GameServer::run() {
    asio::ip::tcp::endpoint ep(asio::ip::tcp::v4(), static_cast<unsigned short>(port_));
    acceptor_.open(ep.protocol());
    acceptor_.set_option(asio::ip::tcp::acceptor::reuse_address(true));
    acceptor_.bind(ep);
    acceptor_.listen();

    std::cout << "[GameServer] Listening on port " << port_ << "\n";
    std::cout << "[GameServer] Ready. Connect with: telnet 127.0.0.1 " << port_ << "\n";

    doAccept();
    scheduleTick();
    ioc_.run(); // blocks until stop()
}

void GameServer::stop() {
    asio::post(ioc_, [this] { acceptor_.close(); });
    ioc_.stop();
}

// ================================================================
// Game tick — 20 TPS, io_context 스레드에서 실행 (추가 잠금 불필요)
// ================================================================

void GameServer::scheduleTick() {
    tickTimer_.expires_after(std::chrono::milliseconds(TICK_MS));
    tickTimer_.async_wait([this](std::error_code ec) {
        if (ec) return;
        onTick();
        scheduleTick();
    });
}

void GameServer::onTick() {
    static auto prev = std::chrono::steady_clock::now();
    auto now = std::chrono::steady_clock::now();
    float dt = std::chrono::duration<float>(now - prev).count();
    prev = now;

    std::lock_guard<std::recursive_mutex> lock(gameMutex_);
    bool anyChanged = false;
    using tp = std::chrono::steady_clock::time_point;
    auto respawnCfg = lua_->getRespawnConfig();
    const auto warnThreshold = std::chrono::milliseconds(respawnCfg.warningMs);
    auto regenCfg = lua_->getRegenConfig();

    // ── 플레이어: 이동 + HP 자동 회복 ────────────────────────────────
    for (auto& [id, p] : players_) {
        if (p->stepTowardTarget(dt)) anyChanged = true;

        if (p->isAlive() && p->hpRegen > 0 && p->hp < p->maxHp) {
            float elapsed = std::chrono::duration<float>(now - p->lastRegenTime).count();
            if (elapsed >= regenCfg.intervalSec) {
                p->hp = std::min(p->hp + p->hpRegen, p->maxHp);
                p->lastRegenTime = now;
                anyChanged = true;
                std::cout << "[GameServer] Player '" << p->name
                          << "' HP regen +" << p->hpRegen
                          << " -> " << p->hp << "/" << p->maxHp << "\n";
            }
        }
    }

    // ── 몬스터: 리스폰 / 어그로 추격 / 반격 ─────────────────────────
    std::vector<Monster*> toSplit;

    for (auto& [id, m] : monsters_) {
        if (!m->isAlive()) {
            // 리스폰 예고 + 실제 리스폰
            if (m->respawnAt != tp{}) {
                if (!m->warningSent && now >= m->respawnAt - warnThreshold) {
                    sendRespawnWarning(m.get());
                    m->warningSent = true;
                }
                if (now >= m->respawnAt) {
                    bool split = doRespawn(m.get());
                    if (split) toSplit.push_back(m.get());
                    anyChanged = true;
                }
            }
            continue;  // 사망한 몬스터는 이동/공격 처리 스킵
        }

        // 어그로 추격 + 반격
        if (m->aggroTargetId != -1) {
            Player* target = getPlayer(m->aggroTargetId);
            if (target && target->isAlive()) {
                float dist = m->pos.distanceTo(target->pos);

                if (dist > m->attackRange) {
                    // ── 사거리 밖: 플레이어를 향해 추격 ───────────────
                    m->targetPos = target->pos;
                    m->speed     = m->aggroSpeed;
                } else {
                    // ── 사거리 안: 정지 후 공격 ───────────────────────
                    m->targetPos = m->pos;   // 제자리 유지
                    m->speed     = 0.f;

                    float cooldownSec = 1.0f / m->attackSpeed;
                    float elapsed = std::chrono::duration<float>(now - m->lastAttackTime).count();
                    if (m->lastAttackTime == tp{} || elapsed >= cooldownSec) {
                        m->lastAttackTime = now;
                        int dmg = lua_->getMonsterDamage(m.get());
                        target->hp = std::max(0, target->hp - dmg);
                        anyChanged = true;

                        std::cout << "[GameServer] Monster '" << m->name
                                  << "' hit player '" << target->name
                                  << "' for " << dmg << " dmg ("
                                  << target->hp << "/" << target->maxHp << ")\n";

                        auto event = json{
                            {"type",       "monster_attack"},
                            {"attackerId", m->id},
                            {"targetId",   target->id},
                            {"damage",     dmg},
                            {"hp",         target->hp},
                            {"maxHp",      target->maxHp}
                        }.dump();
                        broadcastEvent(event);

                        if (!target->isAlive()) {
                            std::cout << "[GameServer] Player '" << target->name
                                      << "' killed by '" << m->name << "'!\n";

                            // 사망 이벤트 브로드캐스트
                            auto deathEvent = json{
                                {"type",     "player_death"},
                                {"playerId", target->id},
                                {"killedBy", m->name}
                            }.dump();
                            broadcastEvent(deathEvent);

                            // 플레이어 리스폰 (Lua formula.deathPenalty — RELOAD formula 핫픽스 가능)
                            auto penalty = lua_->getDeathPenalty(target);
                            if (penalty.resetLevel >= 0) target->level = penalty.resetLevel;
                            if (penalty.resetExp   >= 0) target->exp   = penalty.resetExp;
                            applyClassStats(target, *lua_);   // maxHp·attackPower·속도 초기화
                            target->hp    = target->maxHp;
                            auto deathMapCfg = lua_->getMapConfig();
                            target->pos    = randomPos(deathMapCfg.width, deathMapCfg.height);
                            target->targetPos   = target->pos;
                            target->lastRegenTime   = now;
                            target->lastAttackTime  = {};

                            // 이 플레이어를 추격 중인 모든 몬스터 어그로 해제
                            for (auto& [mid, mon] : monsters_) {
                                if (mon->aggroTargetId == target->id) {
                                    mon->aggroTargetId = -1;
                                    mon->speed    = 0.f;
                                    mon->targetPos = mon->pos;
                                }
                            }
                        }
                    }
                }
            } else {
                // 대상 없음/사망 → 어그로 해제, 정지
                m->aggroTargetId = -1;
                m->speed = 0.f;
                m->targetPos = m->pos;
            }
        }

        if (m->stepTowardTarget(dt)) anyChanged = true;
    }

    // 분열 생성 (monsters_ 이터레이션 완료 후)
    for (Monster* parent : toSplit) {
        splitSpawn(parent);
        anyChanged = true;
    }

    if (anyChanged) broadcastAll();
}

void GameServer::broadcastAll() {
    if (!wsServer_) return;
    wsServer_->broadcastExcept(-2, [this](int pid) {   // -2 = 아무도 제외 안 함
        return getStateJson(pid);
    });
}

void GameServer::broadcastEvent(const std::string& eventJson) {
    if (!wsServer_) return;
    wsServer_->broadcastExcept(-2, [&eventJson](int) { return eventJson; });
}

// ================================================================
// Monster respawn
// ================================================================

void GameServer::scheduleRespawn(Monster* m) {
    static std::mt19937 rng(std::random_device{}());
    auto cfg = lua_->getRespawnConfig();
    std::uniform_int_distribution<int> delayDist(cfg.minMs, cfg.maxMs);

    int delayMs   = delayDist(rng);
    m->respawnAt  = std::chrono::steady_clock::now()
                  + std::chrono::milliseconds(delayMs);
    m->warningSent = false;
    std::cout << "[GameServer] Monster '" << m->name
              << "' will respawn in " << (delayMs / 1000.f) << "s\n";
}

bool GameServer::doRespawn(Monster* m) {
    static std::mt19937 rng(std::random_device{}());
    static std::uniform_int_distribution<int> pct(0, 99);
    static std::uniform_real_distribution<float> angleDist(0.f, 2.f * PI_F);
    static std::uniform_real_distribution<float> rDist(0.f, 1.f);

    auto cfg = lua_->getRespawnConfig();

    // ── 1. 레벨 변동 (Lua formula.respawnLevel) ─────────────────────
    m->level = lua_->getRespawnLevel(m->baseLevel, m->level);

    // ── 2. 엘리트 변이 판정 (Lua cfg) ───────────────────────────────
    bool becomeElite = (pct(rng) < cfg.eliteChance);
    m->isElite = becomeElite;

    // ── 3. 몬스터 스탯 (Lua formula.monsterBaseStats) ───────────────
    auto stats = lua_->getMonsterBaseStats(m->level, becomeElite);
    m->maxHp = stats.maxHp;
    m->coin  = stats.coin;
    m->name  = becomeElite ? ("★ " + m->baseName) : m->baseName;
    m->hp    = m->maxHp;

    // ── 4. AI 파라미터 적용 (Lua formula.monsterAI) ─────────────────
    auto ai = lua_->getMonsterAI(m);
    m->aggroSpeed  = ai.aggroSpeed;
    m->attackSpeed = ai.attackSpeed;

    // ── 5. 구역 귀환: homePos 반경 내 랜덤 ──────────────────────────
    auto mapCfg = lua_->getMapConfig();
    float angle = angleDist(rng);
    float r     = std::sqrtf(rDist(rng)) * cfg.homeRadius;
    float nx = m->homePos.x + r * std::cosf(angle);
    float ny = m->homePos.y + r * std::sinf(angle);
    m->pos       = { std::max(0.f, std::min(mapCfg.width  - 0.001f, nx)),
                     std::max(0.f, std::min(mapCfg.height - 0.001f, ny)) };
    m->targetPos = m->pos;

    // ── 6. 타이머·플래그·어그로 해제 ────────────────────────────────
    m->respawnAt     = {};
    m->warningSent   = false;
    m->aggroTargetId = -1;
    m->speed         = 0.f;

    std::cout << "[GameServer] Monster '" << m->name
              << "' (lv" << m->level << ") respawned at " << m->pos.toString()
              << (becomeElite ? " [ELITE]" : "") << "\n";

    // ── 7. 군집 분열 판정 ────────────────────────────────────────────
    return (pct(rng) < cfg.splitChance);
}

void GameServer::sendRespawnWarning(Monster* m) {
    if (!wsServer_) return;
    // homePos 기준으로 예고 (클라이언트가 대략 어느 구역에 리스폰될지 파악)
    auto msg = json{
        {"type", "respawn_soon"},
        {"id",   m->id},
        {"name", m->baseName},
        {"x",    m->homePos.x},
        {"y",    m->homePos.y}
    }.dump();
    wsServer_->broadcastExcept(-2, [&msg](int) { return msg; });
    std::cout << "[GameServer] Respawn warning: " << m->baseName << "\n";
}

void GameServer::splitSpawn(Monster* parent) {
    auto cfg = lua_->getRespawnConfig();
    if ((int)monsters_.size() >= cfg.maxMonsters) return;  // 인구 상한

    static std::mt19937 rng(std::random_device{}());
    static std::uniform_real_distribution<float> angleDist(0.f, 2.f * PI_F);
    static std::uniform_real_distribution<float> rDist(0.f, 1.f);

    int newId    = nextMonsterId_++;
    int newLevel = std::max(1, parent->level - 1);  // 자식은 한 단계 낮게

    auto child = std::make_unique<Monster>(newId, parent->baseName, newLevel);

    // homePos 반경 절반 안에 스폰
    auto mapCfg = lua_->getMapConfig();
    float angle = angleDist(rng);
    float r     = std::sqrtf(rDist(rng)) * (cfg.homeRadius * 0.5f);
    float nx = parent->homePos.x + r * std::cosf(angle);
    float ny = parent->homePos.y + r * std::sinf(angle);
    child->pos = child->targetPos = child->homePos = {
        std::max(0.f, std::min(mapCfg.width  - 0.001f, nx)),
        std::max(0.f, std::min(mapCfg.height - 0.001f, ny))
    };

    // Lua에서 스탯 조회
    auto stats = lua_->getMonsterBaseStats(newLevel, false);
    child->maxHp = stats.maxHp;
    child->hp    = child->maxHp;
    child->coin  = stats.coin;

    // AI 파라미터 적용
    auto ai = lua_->getMonsterAI(child.get());
    child->aggroSpeed  = ai.aggroSpeed;
    child->attackSpeed = ai.attackSpeed;

    Monster* ptr = child.get();
    monsters_[newId] = std::move(child);
    lua_->registerMonster(ptr);

    std::cout << "[GameServer] Monster '" << ptr->name
              << "' (ID=" << newId << " lv" << newLevel
              << ") split-spawned at " << ptr->pos.toString() << "\n";
}

void GameServer::doAccept() {
    acceptor_.async_accept(
        [this](std::error_code ec, asio::ip::tcp::socket socket) {
            if (ec) {
                if (ec != asio::error::operation_aborted)
                    std::cerr << "[GameServer] accept error: " << ec.message() << "\n";
                return;
            }
            auto ep = socket.remote_endpoint();
            std::cout << "[GameServer] New connection from "
                      << ep.address().to_string() << ":" << ep.port() << "\n";
            std::make_shared<Session>(std::move(socket), *this)->start();
            doAccept();
        });
}

// ================================================================
// Protocol (line-based text)
//   CONNECT <name> [level:<N>] [gender:<N>]
//   ATTACK <monster_id> [damage:<N>]
//   STRUCK <monster_id>
//   KILL <monster_id>
//   BUFF ADD|REMOVE <buff_id>
//   ITEM USE <item_id>
//   QUEST ACCEPT|COMPLETE|REMOVE <quest_id>
//   STATUS
//   RELOAD <module_name>
//   QUIT
// ================================================================

std::string GameServer::processCommand(int playerId, const std::string& line) {
    // Serialize all game-state access (recursive: inner game actions also lock)
    std::lock_guard<std::recursive_mutex> lock(gameMutex_);
    std::istringstream ss(line);
    std::string cmd;
    ss >> cmd;
    std::transform(cmd.begin(), cmd.end(), cmd.begin(), ::toupper);

    if (cmd == "CONNECT") {
        std::string name; ss >> name;
        if (name.empty()) return "ERR: CONNECT <name> [level:<N>] [gender:<N>] [job:<warrior|archer>]";
        int level = 1, gender = 0;
        std::string job = "warrior";
        std::string tok;
        while (ss >> tok) {
            if (tok.rfind("level:", 0) == 0)  level  = std::stoi(tok.substr(6));
            if (tok.rfind("gender:", 0) == 0) gender = std::stoi(tok.substr(7));
            if (tok.rfind("job:", 0) == 0)    job    = tok.substr(4);
        }
        createPlayer(playerId, name, level, gender, job);
        return "OK: Connected as " + name + " (lv=" + std::to_string(level) + ", job=" + job + ")";
    }

    Player* player = getPlayer(playerId);
    if (!player) return "ERR: Not connected. Use CONNECT <name> first.";

    if (cmd == "ATTACK") {
        int monsterId = 0; ss >> monsterId;
        int damage = 10;
        std::string tok;
        while (ss >> tok)
            if (tok.rfind("damage:", 0) == 0) damage = std::stoi(tok.substr(7));
        Monster* m = getMonster(monsterId);
        if (!m) return "ERR: Monster " + std::to_string(monsterId) + " not found";
        float dist = player->pos.distanceTo(m->pos);
        if (!playerAttack(player, m, damage))
            return "ERR: Too far from " + m->name + " (dist=" + std::to_string(dist).substr(0,5)
                   + ", need <=" + std::to_string(player->attackRange).substr(0,4) + ")";
        return m->isAlive()
            ? "OK: Attacked " + m->name + " (hp=" + std::to_string(m->hp) + ")"
            : "OK: Killed " + m->name;
    }

    if (cmd == "STRUCK") {
        int monsterId = 0; ss >> monsterId;
        Monster* m = getMonster(monsterId);
        if (!m) return "ERR: Monster not found";
        playerStruck(player, m);
        return "OK: Struck by " + m->name;
    }

    if (cmd == "KILL") {
        int monsterId = 0; ss >> monsterId;
        Monster* m = getMonster(monsterId);
        if (!m) return "ERR: Monster not found";
        playerKillMonster(player, m);
        return "OK: Killed " + m->name;
    }

    if (cmd == "BUFF") {
        std::string sub; ss >> sub;
        std::transform(sub.begin(), sub.end(), sub.begin(), ::toupper);
        int buffId = 0; ss >> buffId;
        if (sub == "ADD")    { playerApplyBuff(player, buffId);  return "OK: Buff " + std::to_string(buffId) + " applied"; }
        if (sub == "REMOVE") { playerRemoveBuff(player, buffId); return "OK: Buff " + std::to_string(buffId) + " removed"; }
        return "ERR: BUFF ADD|REMOVE <id>";
    }

    if (cmd == "ITEM") {
        std::string sub; ss >> sub;
        std::transform(sub.begin(), sub.end(), sub.begin(), ::toupper);
        int itemId = 0; ss >> itemId;
        if (sub == "USE") { playerUseItem(player, itemId); return "OK: Used item " + std::to_string(itemId); }
        return "ERR: ITEM USE <id>";
    }

    if (cmd == "QUEST") {
        std::string sub; ss >> sub;
        std::transform(sub.begin(), sub.end(), sub.begin(), ::toupper);
        int questId = 0; ss >> questId;
        if (sub == "ACCEPT")   { playerAcceptQuest(player, questId);   return "OK: Quest " + std::to_string(questId) + " accepted"; }
        if (sub == "COMPLETE") { playerCompleteQuest(player, questId); return "OK: Quest " + std::to_string(questId) + " completed"; }
        if (sub == "REMOVE")   { playerRemoveQuest(player, questId);   return "OK: Quest " + std::to_string(questId) + " removed"; }
        return "ERR: QUEST ACCEPT|COMPLETE|REMOVE <id>";
    }

    if (cmd == "STATUS") {
        player->printStatus();
        std::ostringstream out;
        out << "Player:" << player->name
            << " lv=" << player->level
            << " hp=" << player->hp << "/" << player->maxHp
            << " coin=" << player->coin;
        return out.str();
    }

    if (cmd == "RELOAD") {
        std::string module; ss >> module;
        if (module.empty()) return "ERR: RELOAD <module_name>";
        std::lock_guard<std::recursive_mutex> lock(gameMutex_);
        bool ok = lua_->reloadScript(module);
        return ok ? "OK: Reloaded " + module : "ERR: Failed to reload " + module;
    }

    if (cmd == "MOVE") {
        float nx = 0.f, ny = 0.f;
        if (!(ss >> nx >> ny)) return "ERR: MOVE <x> <y>";
        auto movMapCfg = lua_->getMapConfig();
        if (nx < 0.f || nx >= movMapCfg.width || ny < 0.f || ny >= movMapCfg.height)
            return "ERR: Out of bounds (map is " + std::to_string((int)movMapCfg.width)
                   + "x" + std::to_string((int)movMapCfg.height) + ")";
        player->targetPos = {nx, ny};   // tick 루프가 pos를 이동시킴
        return "OK: Moving to (" + std::to_string(nx).substr(0,5)
               + ", " + std::to_string(ny).substr(0,5) + ")";
    }

    if (cmd == "LOOK") {
        std::ostringstream out;
        out << "You are at " << player->pos.toString() << "\n";

        for (auto& [mid, m] : monsters_) {
            float dist = player->pos.distanceTo(m->pos);
            char buf[128];
            std::snprintf(buf, sizeof(buf), "  Monster:%s(id=%d) @ %s  dist=%.1f  hp=%d/%d%s",
                m->name.c_str(), m->id, m->pos.toString().c_str(), dist,
                m->hp, m->maxHp,
                dist <= player->attackRange ? " [IN RANGE]" : "");
            out << buf << "\n";
        }
        for (auto& [pid, p] : players_) {
            if (pid == playerId) continue;
            float dist = player->pos.distanceTo(p->pos);
            char buf[128];
            std::snprintf(buf, sizeof(buf), "  Player:%s(id=%d) @ %s  dist=%.1f",
                p->name.c_str(), p->id, p->pos.toString().c_str(), dist);
            out << buf << "\n";
        }
        std::string s = out.str();
        if (!s.empty() && s.back() == '\n') s.pop_back();
        return s;
    }

    if (cmd == "QUIT" || cmd == "EXIT") return "BYE";

    return "ERR: Unknown command '" + cmd + "'";
}

// ================================================================
// JSON state for web client
// ================================================================

std::string GameServer::getStateJson(int playerId) {
    std::lock_guard<std::recursive_mutex> lock(gameMutex_);

    json j;
    j["type"] = "state";

    Player* p = getPlayer(playerId);
    if (p) {
        j["player"] = {
            {"id",          p->id},    {"name",        p->name},
            {"level",       p->level}, {"hp",          p->hp},
            {"maxHp",       p->maxHp}, {"coin",        p->coin},
            {"x",           p->pos.x}, {"y",           p->pos.y},
            {"tx",          p->targetPos.x}, {"ty",    p->targetPos.y},
            {"speed",       p->speed},
            {"attackRange", p->attackRange},
            {"attackSpeed", p->attackSpeed},
            {"attackPower", p->attackPower},
            {"hpRegen",     p->hpRegen},
            {"job",         p->job},
            {"exp",         p->exp},
            {"expToNext",   lua_->getExpToLevel(p->level)}
        };
    } else {
        j["player"] = nullptr;
    }

    json monsters = json::array();
    for (auto& [id, m] : monsters_) {
        monsters.push_back({
            {"id",      m->id},   {"name",    m->name},
            {"level",   m->level},{"hp",      m->hp},
            {"maxHp",   m->maxHp},{"coin",    m->coin},
            {"alive",   m->isAlive()},
            {"isElite", m->isElite},
            {"x",       m->pos.x},{"y",       m->pos.y},
            {"tx",      m->targetPos.x},{"ty", m->targetPos.y},
            {"speed",   m->speed}
        });
    }
    j["monsters"] = monsters;

    json players = json::array();
    for (auto& [id, pl] : players_) {
        if (id == playerId) continue;
        players.push_back({
            {"id",    pl->id},  {"name",  pl->name},
            {"x",     pl->pos.x},{"y",    pl->pos.y},
            {"tx",    pl->targetPos.x},{"ty", pl->targetPos.y},
            {"speed", pl->speed}
        });
    }
    j["players"] = players;

    return j.dump();
}
