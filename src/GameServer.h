#pragma once
#include <memory>
#include <map>
#include <string>
#include <atomic>
#include <mutex>
#include <deque>
#include <chrono>

#include <asio.hpp>
#include "Map.h"
#include "Vec2.h"

class LuaEngine;
class Player;
class Monster;
class GameServer;
class WsServer;

// ── Per-client async session ────────────────────────────────────────
class Session : public std::enable_shared_from_this<Session> {
public:
    Session(asio::ip::tcp::socket socket, GameServer& server);
    void start();

private:
    void doRead();
    void doWrite(const std::string& msg);
    void flushWrite();

    asio::ip::tcp::socket   socket_;
    asio::streambuf         readBuf_;
    GameServer&             server_;
    int                     playerId_;
    std::deque<std::string> writeQueue_;
    bool                    writing_ = false;

    static std::atomic<int> nextId_;
};

// ── 바닥 아이템 (드롭된 아이템) ───────────────────────────────────
struct GroundItem {
    int   id;        // 고유 ID
    int   itemId;    // 아이템 종류
    int   qty;       // 수량
    Vec2  pos;       // 위치
    std::chrono::steady_clock::time_point dropTime;  // 드롭 시각
};

// ── Game server ─────────────────────────────────────────────────────
class GameServer {
public:
    GameServer();
    ~GameServer();

    bool initialize(const std::string& scriptDir, int port = 7000);
    void run();    // blocking — returns when stop() is called
    void stop();

    // Game actions (internally lock gameMutex_)
    bool playerAttack(Player*, Monster*, int damage = 10);
    void playerStruck(Player*, Monster*);
    void playerKillMonster(Player*, Monster*);
    void playerApplyBuff(Player*, int buffId);
    void playerRemoveBuff(Player*, int buffId);
    void playerUseItem(Player*, int itemId);
    void playerAcceptQuest(Player*, int questId);
    void playerCompleteQuest(Player*, int questId);
    void playerRemoveQuest(Player*, int questId);

    // Object management
    Player*  createPlayer(int id, const std::string& name, int level = 1, int gender = 0,
                          const std::string& job = "warrior");
    Monster* createMonster(int id, const std::string& name, int level = 1, float x = -1.f, float y = -1.f);
    Player*  getPlayer(int id);
    Monster* getMonster(int id);
    void     removePlayer(int id);

    void        spawnMonstersFromLua();  // Lua formula.getSpawnTable() 에서 초기 몬스터 생성
    void        runTestScenario();
    std::string processCommand(int playerId, const std::string& line);
    std::string getStateJson(int playerId);  // JSON map state for web client

    // Real-time broadcast — wire up via setWsServer() before run()
    void setWsServer(WsServer* ws) { wsServer_ = ws; }
    void broadcastToOthers(int excludePlayerId);
    void broadcastAll();                           // tick 완료 후 전체 clients에게 상태 전송
    void broadcastEvent(const std::string& json);  // 단일 이벤트를 모든 WS clients에게 전송

private:
    // Game state
    std::unique_ptr<LuaEngine>              lua_;
    std::map<int, std::unique_ptr<Player>>  players_;
    std::map<int, std::unique_ptr<Monster>> monsters_;
    std::map<int, GroundItem>               groundItems_;  // 바닥 아이템
    std::recursive_mutex                    gameMutex_;
    Map                                     map_;

    // WebSocket broadcast (optional, set after WsServer is created)
    WsServer* wsServer_ = nullptr;

    // Async network
    asio::io_context        ioc_;
    asio::ip::tcp::acceptor acceptor_;
    int                     port_ = 7000;

    // Game tick (기본 50ms = 20 TPS, Lua formula.getTickConfig 으로 오버라이드 가능)
    static constexpr int   TICK_MS     = 50;  // fallback
    asio::steady_timer   tickTimer_;
    int nextMonsterId_ = 9000;          // 분열 생성 몬스터 ID 카운터
    int nextGroundItemId_ = 1;          // 바닥 아이템 ID 카운터

    static constexpr float GROUND_ITEM_EXPIRE_SEC = 60.0f;  // 바닥 아이템 만료 시간
    static constexpr float PICKUP_RANGE = 3.0f;             // 아이템 줍기 거리

    void scheduleTick();
    void onTick();

    void doAccept();
    void scheduleRespawn(Monster* m);           // 사망 시 리스폰 타이머 설정
    bool doRespawn(Monster* m);                 // 리스폰 처리, 분열 여부 반환
    void sendRespawnWarning(Monster* m);        // 리스폰 예고 브로드캐스트
    void splitSpawn(Monster* parent);           // 군집 분열: 자식 몬스터 생성
    void distributeExp(Monster* monster);      // 기여도 기반 경험치 분배
    void grantExpAndLevelUp(Player* player, int expGain);  // EXP 지급 + 레벨업 처리
    void dropItemsFromMonster(Monster* monster); // 몬스터 사망 시 아이템 드롭
};
