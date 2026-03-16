# GameServer 설계 문서

> C++17 · sol2 v3.3.0 · Standalone Asio · Lua 5.x · 100×100 지도

---

## 목차

1. [프로젝트 개요](#1-프로젝트-개요)
2. [파일 구조](#2-파일-구조)
3. [아키텍처](#3-아키텍처)
4. [빌드 방법](#4-빌드-방법)
5. [지도 및 위치 시스템](#5-지도-및-위치-시스템)
6. [네트워크 프로토콜](#6-네트워크-프로토콜)
7. [웹 클라이언트 (HTTP+WebSocket)](#7-웹-클라이언트-httpwebsocket)
8. [실시간 멀티플레이어 Broadcast](#8-실시간-멀티플레이어-broadcast)
9. [Lua 스크립트 연동](#9-lua-스크립트-연동)
10. [핫 리로드](#10-핫-리로드)
11. [전투 시스템](#11-전투-시스템)
12. [성장 시스템](#12-성장-시스템)
13. [인벤토리·아이템·상점 시스템](#13-인벤토리아이템상점-시스템)
14. [직업 시스템](#14-직업-시스템)
15. [클라이언트 자동 행동](#15-클라이언트-자동-행동)
16. [클래스 레퍼런스](#16-클래스-레퍼런스)
17. [테스트 세션 예시](#17-테스트-세션-예시)

---

## 1. 프로젝트 개요

C++17로 작성된 MMORPG 프로토타입 게임 서버. 게임 로직은 Lua 스크립트로 구현하며, sol2를 통해 C++↔Lua 바인딩을 처리한다. 네트워크는 Standalone Asio 비동기 I/O 기반이며, 100×100 타일 지도 위에서 다수의 플레이어가 실시간으로 상호작용한다.

### 핵심 특징

| 항목 | 내용 |
| --- | --- |
| 언어 | C++17 |
| Lua 바인딩 | sol2 v3.3.0 (header-only) |
| 네트워크 | Standalone Asio (header-only, 비동기) |
| 빌드 | CMake 3.20+, Visual Studio 2022 |
| TCP 포트 | 7000 (텍스트 프로토콜) |
| HTTP+WS 포트 | 7001 (웹 클라이언트 + WebSocket JSON) |
| 지도 크기 | 100 × 100 |
| 게임 루프 | 20 TPS (50ms 주기, asio steady_timer) |
| 실시간 업데이트 | 플레이어 행동 시 같은 맵의 모든 클라이언트에 broadcast |

---

## 2. 파일 구조

```text
project/
├── CMakeLists.txt
├── docs.md                         ← 이 문서
├── src/
│   ├── main.cpp                    ← 진입점: 몬스터 배치, 테스트, 서버 시작
│   ├── Vec2.h                      ← 2D 좌표 구조체 (header-only)
│   ├── Map.h                       ← 지도 상수 및 범위 검사 (header-only)
│   ├── Entity.h                    ← 플레이어/몬스터 공통 베이스 클래스
│   ├── Player.h / Player.cpp       ← 플레이어 데이터 (직업, EXP, HP 재생 등)
│   ├── Monster.h / Monster.cpp     ← 몬스터 데이터 (어그로, 공격속도 등)
│   ├── LuaEngine.h / LuaEngine.cpp ← sol2 기반 Lua VM 관리 + 직업 스탯 조회
│   ├── GameServer.h / GameServer.cpp← Asio TCP 서버 + 게임 루프 + 전투/성장 로직
│   ├── WsServer.h / WsServer.cpp   ← HTTP+WebSocket 서버 (별도 스레드)
│   └── sha1.h                      ← WebSocket 핸드셰이크 키 생성 (header-only)
├── web/
│   └── index.html                  ← 웹 클라이언트 (Canvas 기반 게임 화면)
└── lua/
    └── EventManager/
        ├── event_manager.lua       ← Pub/Sub 이벤트 버스
        ├── buff_manager.lua        ← 버프 적용/해제
        ├── item_manager.lua        ← 아이템 사용
        ├── quest_manager.lua       ← 퀘스트 수락/완료
        ├── monster_manager.lua     ← 몬스터 관련 스크립트
        ├── player_manager.lua      ← 플레이어 스크립트 관리자
        ├── class_manager.lua       ← 직업별 스탯 정의 (RELOAD로 핫픽스 가능)
        ├── formula.lua             ← 전투·경험치·스폰·AI·맵 공식 통합 모듈 (RELOAD 핫픽스)
        ├── buff/1000.lua
        ├── item/1000.lua           ← 소형 체력물약 (heal 50, 100코인)
        ├── item/1001.lua           ← 중형 체력물약 (heal 100, 300코인)
        ├── item/1002.lua           ← 대형 체력물약 (heal 150, 500코인)
        ├── quest/1000.lua
        └── player/base.lua         ← 기본 플레이어 이벤트 핸들러 (onKill 코인 획득)
```

---

## 3. 아키텍처

### 3.1 전체 흐름

```text
브라우저 (web/index.html)
    │  HTTP → index.html 서빙
    │  WS  JSON 메시지
    ▼
WsServer (포트 7001, 별도 스레드)
    │  WsSession per-client
    │  processCommand() 호출
    ▼
GameServer (게임 로직, gameMutex_)
    │
    ├── 20 TPS 게임 루프 (onTick)
    │     ├── 플레이어 HP 재생 (5초 주기)
    │     ├── 몬스터 어그로 이동
    │     ├── 몬스터 자동 반격
    │     ├── 플레이어 사망 감지 및 리스폰
    │     └── 리스폰 대기열 처리
    │
    ├── Map (위치/범위 검사)
    ├── groundItems_ (드롭 아이템 관리, 60초 만료)
    │
    └── LuaEngine (sol2)
          ├── event_manager.publish → onAttack / onKill / onStruck
          ├── buff_manager / item_manager / quest_manager
          ├── player_manager → player/base.lua (onKill 코인 획득)
          ├── class_manager.getStats(job, level) → 직업별 스탯 조회
          ├── formula.lua → 전투·경험치·스폰·AI·맵·드롭·기여도 공식
          └── item/1000~1002 → 체력물약 (사용 시 HP 회복)

TCP 클라이언트 (telnet, 포트 7000)
    │  텍스트 라인 프로토콜
    ▼
Session → processCommand() (WsSession과 동일한 GameServer 공유)
```

### 3.2 멀티플레이어 실시간 Broadcast 흐름

```text
플레이어A (WebSocket)
    │  { cmd: "move", x: 30, y: 30 }
    ▼
WsSession::wsHandleText()
    │  processCommand() → 위치 갱신
    │  wsSend(ok) + wsSend(getStateJson(A))   ← A에게 응답
    │
    └── gs_.broadcastToOthers(A의 id)
          │
          └── WsServer::broadcastExcept(A)
                │  세션 스냅샷 (B, C, D...)
                │  각 세션에 pushJson(getStateJson(세션id))
                ▼
          플레이어B, C, D 화면 즉시 갱신
```

게임 루프 이벤트(몬스터 반격, 사망, 레벨업 등)는 `broadcastEvent(json)`으로 전체 클라이언트에 동시 전송된다.

### 3.3 WsServer 스레드 모델

```text
main thread            WsServer thread
─────────────          ─────────────────
GameServer::run()      WsServer ioc.run()
  (TCP+게임루프)          (HTTP+WS, 포트 7001)
  onTick() 20TPS         WsSession (per-client)
    → broadcastEvent()     wsHandleText()
                             → gs_.processCommand()  ← gameMutex_ 획득
                             → gs_.broadcastToOthers()
                                 → WsServer::broadcastExcept()
                                     → pushJson() via asio::post
```

#### 스레드 안전성 포인트

| 작업 | 보호 방법 |
| --- | --- |
| players_/monsters_ 읽기·쓰기 | `gameMutex_` (recursive_mutex) |
| sessions_ 맵 읽기·쓰기 | `sessionsMutex_` (mutex) |
| wsSend / pushJson | `asio::post` → WsServer ioc 스레드에서 순차 실행 |
| Lua VM 호출 | `gameMutex_` 보호 내에서만 호출 |

### 3.4 Lua 객체 영구 동일성

`event_manager`는 Lua 테이블을 키로 플레이어를 관리한다. 매 호출마다 `sol::make_object`를 새로 만들면 같은 플레이어라도 다른 키로 인식한다. LuaEngine은 ID별로 객체를 캐시한다.

```cpp
void LuaEngine::registerPlayer(Player* player) {
    if (playerObjects.count(player->id)) return;          // 이미 등록됨
    playerObjects[player->id] = sol::make_object(lua, player);
    luaCall("player_manager", "initPlayer", playerObj(player)); // 이벤트 구독
}
sol::object LuaEngine::playerObj(Player* p) {
    return playerObjects.at(p->id);  // 항상 동일 객체 반환
}
```

---

## 4. 빌드 방법

### 4.1 사전 요구사항

- CMake 3.20 이상
- Visual Studio 2022 (MSVC v143)
- Lua 5.x 개발 라이브러리
- 인터넷 연결 (FetchContent가 sol2, asio, nlohmann/json 자동 다운로드)

### 4.2 빌드

```bat
cmake -S . -B build
cmake --build build --config Release
```

FetchContent가 자동으로 다운로드하는 라이브러리:

| 라이브러리 | 버전 | 용도 |
| --- | --- | --- |
| sol2 | v3.3.0 | Lua C++ 바인딩 |
| Standalone Asio | asio-1-30-2 | 비동기 I/O |
| nlohmann/json | latest | WebSocket JSON 직렬화 |

> **CMake 정책**: `set(CMAKE_POLICY_VERSION_MINIMUM 3.5)` — nlohmann/json 서브프로젝트의 구버전 cmake_minimum_required 충돌 해결

### 4.3 실행

```bat
build\Release\GameServer.exe               REM 기본 포트 7000(TCP), 7001(WS)
build\Release\GameServer.exe 8000 8001    REM 포트 지정
```

실행 흐름:

1. `lua/EventManager` 디렉터리 자동 탐색 (최대 3단계 상위 디렉터리)
2. Lua 스크립트 초기화 (event/buff/item/quest/monster/player/class/formula 매니저 + 개별 스크립트 로드)
3. `formula.getSpawnTable()`에서 몬스터 스폰 테이블 로드 및 배치
4. `runTestScenario()` — Lua 연동 검증
5. WsServer 시작 (포트 7001, 별도 스레드)
6. TCP 서버 + 20 TPS 게임 루프 시작 (포트 7000, main thread blocking)

웹 클라이언트 접속: `http://localhost:7001`

### 4.4 CMakeLists.txt 핵심

```cmake
# Standalone Asio
FetchContent_Declare(asio
    GIT_REPOSITORY https://github.com/chriskohlhoff/asio.git
    GIT_TAG        asio-1-30-2
)
FetchContent_GetProperties(asio)
if(NOT asio_POPULATED)
    FetchContent_Populate(asio)
endif()

target_compile_definitions(${PROJECT_NAME} PRIVATE
    ASIO_STANDALONE
    _WIN32_WINNT=0x0601     # Windows 7+
)
target_link_libraries(${PROJECT_NAME} PRIVATE
    ${LUA_LIBRARIES} sol2 nlohmann_json::nlohmann_json ws2_32 mswsock
)
```

---

## 5. 지도 및 위치 시스템

### 5.1 구조

```text
Vec2 { float x, y }           ← Entity::pos, Entity::targetPos
Map  { WIDTH=100, HEIGHT=100 }
```

#### Vec2.h

```cpp
struct Vec2 {
    float x = 0.f, y = 0.f;
    float distanceTo(const Vec2& o) const;  // 유클리드 거리
    std::string toString() const;            // "(x.x, y.y)"
};
```

#### Map.h

```cpp
class Map {
public:
    static constexpr float WIDTH  = 100.f;
    static constexpr float HEIGHT = 100.f;

    static bool isInBounds(float x, float y);
};
```

### 5.2 이동 시스템 (보간)

서버는 `pos`(현재 위치)와 `targetPos`(목적지)를 분리 관리한다. 게임 루프(20 TPS)마다 `pos`를 `targetPos` 방향으로 `speed` 만큼 이동시킨다.

클라이언트는 서버에서 `tx`, `ty`, `speed`를 받아 **requestAnimationFrame(60fps)** 기반으로 위치를 선형 보간하여 부드러운 이동을 구현한다.

```json
// state → player/monster 필드
{ "x": 20.1, "y": 20.1,   // 서버 실제 위치
  "tx": 45.0, "ty": 30.0, // 목적지
  "speed": 5.0 }           // 이동속도 (units/sec)
```

### 5.3 몬스터 초기 배치 (Lua formula.getSpawnTable)

스폰 테이블은 `formula.lua`에서 정의된다. `RELOAD formula` 후 다음 서버 시작 시 반영.

```lua
function formula.getSpawnTable()
    return {
        { id = 2000, name = "Goblin",      level = 1, x = 20, y = 20 },
        { id = 2001, name = "Orc",         level = 1, x = 50, y = 50 },
        { id = 2002, name = "Dark Knight", level = 1, x = 80, y = 80 },
    }
end
```

HP/코인은 `formula.monsterBaseStats(level, isElite)`로 산출. AI 파라미터는 `formula.monsterAI(monster)`로 설정.

### 5.4 플레이어 스폰

`CONNECT` 명령 또는 WebSocket `connect` 메시지 수신 시 `formula.getMapConfig()` 범위 내 무작위 위치에 스폰된다. 사망 후 리스폰도 동일하게 무작위 위치에 재배치된다.

---

## 6. 네트워크 프로토콜

### 6.1 TCP 텍스트 프로토콜 (포트 7000)

라인 기반 텍스트 (`\r\n` 구분). telnet 또는 TCP 클라이언트로 접속.

```text
telnet 127.0.0.1 7000
```

#### 전체 명령 목록

| 명령 | 구문 | 설명 |
| --- | --- | --- |
| CONNECT | `CONNECT <name> [level:<N>] [gender:<N>] [job:<str>]` | 플레이어 생성, 랜덤 스폰 |
| MOVE | `MOVE <x> <y>` | 위치 이동 (0~99.9) |
| LOOK | `LOOK` | 현재 위치 + 전체 엔티티 거리 출력 |
| ATTACK | `ATTACK <monster_id>` | 몬스터 공격 (거리 ≤ attackRange, 서버 공격력 적용) |
| STRUCK | `STRUCK <monster_id>` | 몬스터에게 피격 이벤트 |
| KILL | `KILL <monster_id>` | 수동 처치 이벤트 (테스트용) |
| BUFF ADD/REMOVE | `BUFF ADD <id>` / `BUFF REMOVE <id>` | 버프 적용/해제 |
| ITEM USE | `ITEM USE <id>` | 인벤토리 아이템 사용 (보유 확인 + 소모) |
| PICKUP | `PICKUP <groundItemId>` | 바닥 아이템 줍기 (거리 ≤ 3.0) |
| SHOP LIST | `SHOP LIST` | 상점 아이템 목록 조회 (요청자에게만 응답) |
| SHOP BUY | `SHOP BUY <itemId> [qty]` | 상점 아이템 구매 (코인 차감) |
| QUEST ACCEPT/COMPLETE/REMOVE | `QUEST <ACTION> <id>` | 퀘스트 처리 |
| STATUS | `STATUS` | 플레이어 현재 상태 |
| RELOAD | `RELOAD <module>` | Lua 스크립트 핫 리로드 |
| QUIT / EXIT | `QUIT` | 연결 종료 |

> **주의**: `ATTACK` 명령의 클라이언트 측 `damage` 파라미터는 무시된다. 서버는 항상 `player->attackPower`를 사용한다 (치트 방지).

#### 오류 응답

| 상황 | 응답 예시 |
| --- | --- |
| CONNECT 전 명령 | `ERR: Not connected. Use CONNECT <name> first.` |
| 공격 사거리 초과 | `ERR: Too far from Goblin (dist=42.4, need <=5.0)` |
| 공격 쿨다운 중 | `ERR: Attack cooldown` |
| 존재하지 않는 몬스터 | `ERR: Monster 9999 not found` |
| MOVE 범위 초과 | `ERR: Out of bounds (map is 100x100)` |

---

## 7. 웹 클라이언트 (HTTP+WebSocket)

### 7.1 서버 구성

**WsServer** (포트 7001)는 두 가지 역할을 한다:

- `GET /` → `web/index.html` 서빙 (HTML/CSS/JS 단일 파일)
- `GET /ws` (Upgrade: websocket) → WebSocket 연결 처리

### 7.2 WebSocket JSON 프로토콜

#### 클라이언트 → 서버

```json
{ "cmd": "connect", "name": "영웅", "level": 1, "gender": 0, "job": "warrior" }
{ "cmd": "move",    "x": 45.5, "y": 30.2 }
{ "cmd": "attack",  "monsterId": 2000 }
{ "cmd": "struck",  "monsterId": 2000 }
{ "cmd": "buff",    "action": "add",    "buffId": 1000 }
{ "cmd": "item",    "action": "USE", "itemId": 1000 }
{ "cmd": "pickup",  "groundItemId": 42 }
{ "cmd": "shop",    "action": "list" }
{ "cmd": "shop",    "action": "buy", "itemId": 1000, "qty": 1 }
{ "cmd": "quest",   "action": "accept", "questId": 1000 }
{ "cmd": "reload",  "module": "buff.1000" }
{ "cmd": "status" }
{ "cmd": "look" }
```

#### 서버 → 클라이언트 (결과 메시지)

```json
{ "type": "ok",    "msg": "OK: Moved to (45.5, 30.2)" }
{ "type": "error", "msg": "ERR: Too far from Goblin ..." }
```

#### 서버 → 클라이언트 (상태 메시지 — 명령 처리 후 자동 전송 및 broadcast)

```json
{
  "type": "state",
  "player": {
    "id": 5000, "name": "영웅", "level": 5,
    "hp": 180, "maxHp": 180, "coin": 150,
    "x": 45.5, "y": 30.2,
    "tx": 60.0, "ty": 40.0,
    "speed": 5.0,
    "attackRange": 5.0,   ← player 객체 내 (직업별 사거리)
    "attackSpeed": 1.0,
    "attackPower": 18,
    "hpRegen": 10,
    "job": "warrior",
    "exp": 250,
    "expToNext": 500,
    "inventory": [
      { "itemId": 1000, "count": 3, "name": "소형 체력물약" },
      { "itemId": 1001, "count": 1, "name": "중형 체력물약" }
    ]
  },
  "groundItems": [
    { "id": 42, "itemId": 1000, "qty": 1, "x": 20.5, "y": 21.3 }
  ],
  "monsters": [
    {
      "id": 2000, "name": "Goblin", "level": 1,
      "hp": 35, "maxHp": 100, "coin": 10,
      "alive": true,
      "x": 20.0, "y": 20.0,
      "tx": 45.5, "ty": 30.2,
      "speed": 4.0
    }
  ],
  "players": [
    { "id": 5001, "name": "용사", "job": "archer", "x": 50.0, "y": 50.0 }
  ]
}
```

#### 서버 → 클라이언트 (실시간 이벤트 메시지 — broadcastEvent)

게임 루프에서 발생하는 전투/성장 이벤트는 별도 JSON으로 전체 클라이언트에 브로드캐스트된다.

```json
// 플레이어 공격 결과
{ "type": "attack_result",
  "attackerId": 5000, "targetId": 2000,
  "damage": 18, "isCrit": false, "targetHp": 82, "targetMaxHp": 100 }

// 몬스터 자동 반격
{ "type": "monster_attack",
  "monsterId": 2000, "monsterName": "Goblin",
  "targetId": 5000, "damage": 5,
  "targetHp": 175, "targetMaxHp": 180 }

// 리스폰 예고
{ "type": "respawn_soon", "monsterId": 2000, "monsterName": "Goblin" }

// 플레이어 레벨업
{ "type": "player_levelup",
  "playerId": 5000, "name": "영웅",
  "level": 6, "maxHp": 200 }

// 플레이어 사망
{ "type": "player_death",
  "playerId": 5000, "name": "영웅",
  "killedBy": "Goblin" }

// 아이템 드롭 (몬스터 처치 시)
{ "type": "item_drop",
  "groundItemId": 42, "itemId": 1000, "itemName": "소형 체력물약",
  "qty": 1, "x": 20.5, "y": 21.3 }

// 바닥 아이템 만료 (60초 경과)
{ "type": "item_expire", "groundItemId": 42 }

// 아이템 줍기
{ "type": "item_pickup",
  "playerId": 5000, "playerName": "영웅",
  "groundItemId": 42, "itemId": 1000, "itemName": "소형 체력물약", "qty": 1 }

// 아이템 사용 (인벤토리)
{ "type": "item_used",
  "playerId": 5000, "itemId": 1000, "itemName": "소형 체력물약" }

// 상점 구매
{ "type": "shop_buy",
  "playerId": 5000, "itemId": 1000, "itemName": "소형 체력물약",
  "qty": 1, "cost": 100 }

// 경험치 분배 (기여도 기반)
{ "type": "exp_distribute",
  "monsterId": 2000, "monsterName": "Goblin",
  "shares": [{ "playerId": 5000, "exp": 80 }, { "playerId": 5001, "exp": 20 }] }

// 상점 목록 (요청자에게만 전송, broadcast 아님)
{ "type": "shop_list",
  "items": [{ "id": 1000, "name": "소형 체력물약", "price": 100, "desc": "HP 50 회복" }, ...] }
```

### 7.3 WebSocket 구현 (WsServer.cpp)

RFC 6455 준수 직접 구현 (외부 WS 라이브러리 미사용):

| 단계 | 내용 |
| --- | --- |
| HTTP Upgrade | `Sec-WebSocket-Key` 추출 → SHA1 기반 `Sec-WebSocket-Accept` 생성 |
| 프레임 읽기 | 2바이트 헤더 → 확장 길이(126/127) → payload + mask 키 처리 |
| 클라이언트→서버 | 항상 마스킹됨 (RFC 필수) |
| 서버→클라이언트 | 마스킹 없음 |
| Ping/Pong | 자동 처리 (opcode 0x9 → 0x8A 응답) |

### 7.4 웹 클라이언트 게임 화면 (web/index.html)

#### 직업 선택 화면 (로그인 폼)

접속 시 이름과 직업을 선택한다.

| 직업 | 표기 | 색상 | 특징 |
| --- | --- | --- | --- |
| 전사 (warrior) | ⚔ 전사 | `#58a6ff` (파랑) | 기본 사거리 5, 높은 HP, 표준 공격속도 |
| 궁수 (archer) | 🏹 궁수 | `#c084fc` (보라) | 원거리 사거리 20, 낮은 HP, 느린 공격속도 |

선택 후 서버에 `job` 필드를 포함하여 connect 메시지를 전송한다.

#### 렌더링 (Canvas 2D, 60fps requestAnimationFrame)

| 엔티티 | 표시 | 색상/아이콘 |
| --- | --- | --- |
| 내 플레이어 (전사) | 파란 원 + ⚔ + 이름 + 공격범위 점선 원 + 쿨다운 arc | `#58a6ff` |
| 내 플레이어 (궁수) | 보라 원 + 🏹 + 이름 + 공격범위 점선 원 + 쿨다운 arc | `#c084fc` |
| 다른 플레이어 | 직업별 색상 원 + 아이콘 + 이름 | 직업별 동일 |
| 몬스터 (HP > 50%) | 빨간 원 | `#f44336` |
| 몬스터 (25% < HP ≤ 50%) | 주황 원 | `#d29922` |
| 몬스터 (HP ≤ 25%) | 진주황 원 | `#ff5500` |
| 몬스터 (사망) | 회색 원 (반투명) | `#555` |
| 공격 이펙트 | 공격자→대상 선 + 크리티컬 시 확산 원 | 크리티컬: 빨간 원 |

HP 바 색상도 동일한 기준 적용. 플레이어 공격 쿨다운은 원 테두리의 주황색 arc로 시각화된다.

#### 사이드바 정보

몬스터 선택 시 오른쪽 패널에 이름, 레벨, HP 바를 표시. 플레이어 정보에는 직업, 공격력, HP 바(빨강), EXP 바(보라)가 포함된다.

#### 조작

| 입력 | 동작 |
| --- | --- |
| 좌클릭 (빈 공간) | 현재 타겟 해제 + 해당 위치로 이동 |
| 좌클릭 (몬스터) | 몬스터 선택 → 자동 추적+공격 시작 |
| 더블클릭 (몬스터) | 몬스터 선택 → 자동 추적+공격 시작 (좌클릭과 동일) |
| 우클릭 (몬스터) | 공격 명령 즉시 전송 (수동) |
| A키 | 자동공격 ON/OFF 토글 |
| Escape | 타겟 선택 해제 + 수동 이동 취소 |
| 드래그 | 화면 팬 (이동 명령 없음) |
| 휠 | 줌 인/아웃 |

드래그 판정: 마우스 이동이 2px를 초과하면 드래그로 판정하여 클릭 이벤트를 무시한다.

#### 통합 전투 시스템 (selectedMonster)

몬스터를 클릭하면 `selectedMonster`로 설정되고, 게임 루프에서 자동으로 추적+공격이 수행된다. 자동공격(`autoAttack`)과 수동 선택을 하나의 변수로 통합 관리한다.

```javascript
// 전투 흐름 (gameLoop 내, 60fps rAF)

// 1. 자동공격 ON + 타겟 없음 + 수동 이동 완료 → 가장 가까운 몬스터 자동 선택
if (autoAttack && !selectedMonster && !manualMoveTarget && p) {
    let nearest = null, minD = Infinity;
    for (const m of gameState.monsters) {
        if (!m.alive) continue;
        const d = Math.hypot(p.x - m.x, p.y - m.y);
        if (d < minD) { minD = d; nearest = m; }
    }
    if (nearest) { selectedMonster = nearest; chaseInRange = false; }
}

// 2. 선택된 몬스터 추적+공격 (수동/자동 공통)
if (selectedMonster && p) {
    const dist  = Math.hypot(p.x - fresh.x, p.y - fresh.y);
    const range = p.attackRange ?? 5.0;

    if (dist > range) {
        // 사거리 밖 → 300ms 간격으로 이동 명령 전송 (사거리의 80% 지점)
        const dest = calcChaseDestination(p.x, p.y, fresh.x, fresh.y, range * 0.8);
        send({ cmd: 'move', x: dest.x, y: dest.y });
    } else {
        // 사거리 진입 시 1회 정지 명령 (서버 이동 취소)
        if (!chaseInRange) { chaseInRange = true; send({ cmd: 'move', x: p.x, y: p.y }); }
        // 공격 쿨다운마다 공격
        send({ cmd: 'attack', monsterId: selectedMonster.id });
    }
}
```

**타겟 해제 조건**:

- 빈 공간 클릭 (이동 명령 동시 전송)
- Escape 키 입력
- 대상 몬스터 사망 (다음 틱에 자동 감지)
- 내 플레이어 사망 (리스폰 시 초기화)

#### 수동 이동 우선 시스템 (manualMoveTarget)

빈 공간을 클릭하면 즉시 타겟이 해제되고 이동이 시작된다. 자동공격이 ON이더라도 **목적지 도착 전까지는 새 타겟을 자동 선택하지 않는다**.

```javascript
// 빈 공간 클릭 시
selectedMonster = null;
manualMoveTarget = { x, y };  // 도착 감지용
send({ cmd: 'move', x, y });

// 매 프레임 도착 여부 확인
if (manualMoveTarget && p) {
    const dist = Math.hypot(p.x - manualMoveTarget.x, p.y - manualMoveTarget.y);
    if (dist < 1.0) manualMoveTarget = null;  // 도착 → 자동타겟 재개
}
```

| 상태 | 자동타겟 동작 |
| --- | --- |
| `manualMoveTarget` 있음 | 억제 (목적지 이동 중) |
| `manualMoveTarget` 없음 + `autoAttack` ON | 가장 가까운 몬스터 자동 선택 |
| 몬스터 클릭 | `manualMoveTarget = null` 즉시 취소 → 선택한 몬스터 추적 |

#### 다중 플레이어 슬롯 배분 (calcChaseDestination)

여러 플레이어가 같은 몬스터를 추적할 때 서로 겹치지 않도록 몬스터 주변의 최적 슬롯을 계산한다.

```javascript
function calcChaseDestination(myX, myY, monX, monY, stopDist) {
    // 인근 플레이어의 몬스터 기준 각도 수집
    const occupied = [];
    for (const pl of gameState.players)
        if (Math.hypot(pl.x - monX, pl.y - monY) < stopDist * 2.5)
            occupied.push(Math.atan2(pl.y - monY, pl.x - monX));

    // 60° 간격 후보 중 점수 최대인 각도 선택
    // score = minSep(다른 플레이어와 최소 각도 차) * 2 - rot(현재 위치에서 회전량)
    const myAng = Math.atan2(myY - monY, myX - monX);
    const step = Math.PI / 3;  // 60°
    const candidates = [myAng];
    for (let i = 1; i <= 5; i++) {
        candidates.push(myAng + step * i);
        candidates.push(myAng - step * i);
    }
    // bestAng 선택 후 (monX + cos(bestAng)*stopDist, monY + sin(bestAng)*stopDist) 반환
}
```

#### 시각적 플레이어 분리 (renderPos spring)

실제 서버 좌표(`pos`)는 변경하지 않고, 렌더링용 좌표(`renderPos`)에만 반발력을 적용하여 화면에서 플레이어가 겹쳐 보이지 않게 한다.

```javascript
const SEP_MIN = 3.0;   // 최소 분리 거리 (units)
const SEP_K   = 0.12;  // spring 계수

// 매 프레임 모든 플레이어 쌍 검사
for (let i = 0; i < players.length; i++)
    for (let j = i + 1; j < players.length; j++) {
        const d = dist(a.renderPos, b.renderPos);
        if (d < SEP_MIN && d > 0.001) {
            const push = (SEP_MIN - d) * SEP_K;
            a.renderPos += normal * push;
            b.renderPos -= normal * push;
        }
    }
```

#### 사망 오버레이

플레이어 사망 시 화면 전체에 반투명 빨간 오버레이와 "💀 사망 패널티 적용" 텍스트가 3초간 표시된다. 이후 자동으로 레벨 절반(올림)으로 리스폰된다.

---

## 8. 실시간 멀티플레이어 Broadcast

### 8.1 세션 레지스트리

`WsServer`는 활성 세션 목록을 관리한다.

```cpp
// WsServer 내부
std::mutex                              sessionsMutex_;
std::map<int, std::weak_ptr<WsSession>> sessions_;

void addSession(int playerId, std::shared_ptr<WsSession>);
void removeSession(int playerId);
```

WS 핸드셰이크 완료 시 `addSession` 호출, 연결 해제(`onDisconnect`) 시 `removeSession` + `gs_.removePlayer()` 호출.

### 8.2 Broadcast 종류

#### broadcastToOthers — 명령 처리 후 상태 동기화

```cpp
void GameServer::broadcastToOthers(int excludePlayerId) {
    wsServer_->broadcastExcept(excludePlayerId, [this](int pid) {
        return getStateJson(pid);  // 각 수신자 관점의 상태 JSON
    });
}
```

#### broadcastEvent — 게임 루프 이벤트 전파

```cpp
void GameServer::broadcastEvent(const std::string& eventJson) {
    if (!wsServer_) return;
    // excludeId = -2 : 모든 세션에 동일 JSON 전송
    wsServer_->broadcastExcept(-2, [&eventJson](int) { return eventJson; });
}
```

`attack_result`, `monster_attack`, `player_levelup`, `player_death` 이벤트에 사용된다.

### 8.3 Broadcast 발동 조건

| 명령 / 이벤트 | Broadcast 방식 | 이유 |
| --- | --- | --- |
| connect, move, attack | broadcastToOthers | 게임 상태 변화 |
| struck, kill, buff, item, quest | broadcastToOthers | 게임 상태 변화 |
| pickup | broadcastEvent (item_pickup) | 바닥 아이템 제거 동기화 |
| 공격 결과, 몬스터 반격, 레벨업, 사망 | broadcastEvent | 게임 루프 이벤트 |
| 아이템 드롭, 만료, 사용, 구매 | broadcastEvent | 게임 상태 동기화 |
| 경험치 분배 | broadcastEvent (exp_distribute) | 기여도별 EXP 안내 |
| shop list | 직접 JSON 응답 (요청자만) | 개인 UI 데이터 |
| status, look, reload | 없음 | 읽기 전용 |
| 오류 응답 | 없음 | 상태 변화 없음 |

### 8.4 플레이어 접속/퇴장 알림

- **접속**: connect broadcast → 다른 클라이언트의 `players` 배열에 신규 플레이어 추가
- **퇴장**: `onDisconnect()` → `removePlayer()` → `broadcastToOthers(-1)` (전체 broadcast)
- **클라이언트**: `players` 배열 크기 변화 감지 → 로그 출력 + 헤더 접속 인원 수 갱신

---

## 9. Lua 스크립트 연동

### 9.1 매니저 로드 순서 (LuaEngine::initialize)

```text
1. event_manager.lua   ← 이벤트 버스 (가장 먼저)
2. item_manager.lua
3. buff_manager.lua
4. quest_manager.lua
5. monster_manager.lua
6. player_manager.lua  ← 플레이어 스크립트 관리자
7. class_manager.lua   ← 직업별 스탯 (RELOAD 핫픽스 가능)
8. formula.lua         ← 전투·경험치·스폰·AI·맵 공식 (RELOAD 핫픽스 가능)
   (registerQuestManagerExtensions — C++ 측 getQuestTable 주입)
9. item.1000 / item.1001 / item.1002 / buff.1000 / quest.1000 / player.base  ← 개별 스크립트
```

### 9.2 Lua 파일 목록

| 파일 | 전역 테이블 | 역할 |
| --- | --- | --- |
| `event_manager.lua` | `event_manager` | Pub/Sub 이벤트 버스 |
| `buff_manager.lua` | `buff_manager` | 버프 적용/해제, 리스너 등록 |
| `item_manager.lua` | `item_manager` | 아이템 등록/사용/상점 목록/아이템 정보 조회 |
| `quest_manager.lua` | `quest_manager` | 퀘스트 수락/완료/제거 |
| `monster_manager.lua` | `monster_manager` | 몬스터 관련 스크립트 |
| `player_manager.lua` | `player_manager` | 플레이어 스크립트 등록/관리 |
| `class_manager.lua` | `class_manager` | 직업·레벨별 스탯 반환 (`getStats(job, level)`) |
| `formula.lua` | `formula` | 전투·경험치·스폰·AI·맵·드롭·기여도 공식 통합 모듈 |
| `buff/1000.lua` | — | 버프 1000: 공격 시 +10코인, 피격 시 -10코인 |
| `item/1000.lua` | — | 소형 체력물약: HP 50 회복, 100코인 |
| `item/1001.lua` | — | 중형 체력물약: HP 100 회복, 300코인 |
| `item/1002.lua` | — | 대형 체력물약: HP 150 회복, 500코인 |
| `quest/1000.lua` | — | 퀘스트 1000: Goblin 5마리 처치 |
| `player/base.lua` | — | 기본 플레이어 스크립트: onKill 시 몬스터 코인 획득 |

### 9.3 C++ 타입 → Lua 바인딩 (sol2)

#### Player (읽기/쓰기 가능)

```cpp
lua.new_usertype<Player>("Player",
    "id",             sol::readonly_property([](const Player& p){ return p.id; }),
    "name",           sol::readonly_property(/* ... */),
    "level",          sol::property(getter, setter),
    "gender",         sol::property(getter, setter),
    "coin",           sol::property(getter, setter),
    "hp",             sol::property(getter, setter),
    "maxHp",          sol::property(getter, setter),
    "exp",            sol::property(getter, setter),
    "job",            sol::readonly_property(/* ... */),
    "addCoin",        &Player::addCoin,
    "AddHP",          &Player::addHP,
    "addHP",          &Player::addHP,        // 소문자 별칭
    "addItem",        &Player::addItem,
    "removeItem",     &Player::removeItem,
    "getItemCount",   &Player::getItemCount,
    "setQuestData",   &Player::setQuestData,
    "addQuestData",   &Player::addQuestData,
    "removeQuestData",&Player::removeQuestData,
    "getCustomData",  [](Player& p, const std::string& key, sol::this_state s) -> sol::table { /* ... */ }
);
```

#### Monster (모두 읽기 전용)

```cpp
lua.new_usertype<Monster>("Monster",
    "id",    sol::readonly_property(/* ... */),
    "name",  sol::readonly_property(/* ... */),
    "type",  sol::readonly_property(/* ... */),  // 항상 "Monster"
    "level", sol::readonly_property(/* ... */),
    "hp",      sol::readonly_property(/* ... */),
    "maxHp",   sol::readonly_property(/* ... */),
    "coin",    sol::readonly_property(/* ... */),
    "isElite", sol::readonly_property(/* ... */)
);
```

### 9.4 이벤트 흐름 예시 (ATTACK → onKill)

```text
C++: playerAttack(player, monster)
  ↓  거리 체크 통과 (dist ≤ player->attackRange)
  ↓  쿨다운 체크 통과 (elapsed ≥ 1.0 / attackSpeed)
  ↓  크리티컬 판정 (formula.getCritical → 확률/배율 조회)
  ↓  actualDamage = isCrit ? attackPower * multiplier : attackPower
  ↓  monster->takeDamage(actualDamage)
  ↓  monster->addDamageContribution(player->id, actualDamage)
  ↓  monster->aggroTargetId = player->id  (어그로 설정)
  ↓  broadcastEvent("attack_result")
  ↓  monster->isAlive() == false
       ↓  scheduleRespawn(monster)  ← formula.getRespawnConfig() 딜레이
       ↓  distributeExp(monster)    ← 기여도 기반 EXP 분배
       ↓  dropItemsFromMonster(monster) ← 아이템 드롭 (확률 기반)
  ↓  publishEvent(player, "onKill", monster)
       ↓  quest.1000::onKill  → player:addQuestData(1000, 2000, 1)
       ↓  player.base::onKill → player:addCoin(monster.coin, 'drop')
```

### 9.5 getCustomData (per-player 커스텀 테이블)

```lua
local data = owner:getCustomData("buff1000")
data.stacks = (data.stacks or 0) + 1
```

C++ 구현: Lua 레지스트리에 `"__custom:<playerId>:<key>"` 키로 테이블 저장 → 핫 리로드 후에도 유지된다.

---

## 10. 핫 리로드

### 10.1 메커니즘

```cpp
bool LuaEngine::reloadScript(const std::string& moduleName) {
    lua["package"]["loaded"][moduleName] = sol::lua_nil;  // 캐시 무효화
    return loadScript(moduleName);                         // 재실행
}
```

### 10.2 Lua 스크립트 안전 패턴

핫 리로드 시 상태 손실을 방지하려면 스크립트 최상단에서 이전 테이블을 계승한다.

```lua
-- ✅ 올바른 예 — 이전 데이터 계승
local scripts = player_manager and player_manager.__scripts or {}
player_manager = { __scripts = scripts }

-- ❌ 잘못된 예 — 리로드 시 데이터 초기화
-- player_manager = {}
```

이벤트 리스너 중복 등록 방지:

```lua
if scripts[script.id] then
    event_manager.registerListener(sname, nil)  -- 기존 리스너 제거
    print(sname .. " hotfix reload")
end
event_manager.registerListener(sname, script)   -- 재등록
```

### 10.3 리로드 가능 모듈

```text
RELOAD class_manager       ← 직업별 스탯 핫픽스 (서버 재시작 불필요)
RELOAD formula             ← 전투·경험치·스폰·AI·맵 공식 핫픽스
RELOAD player_manager      ← 플레이어 매니저 재로드
RELOAD player.base         ← 기본 플레이어 스크립트 재로드
RELOAD buff_manager
RELOAD buff.1000
RELOAD item_manager / item.1000
RELOAD quest_manager / quest.1000
```

`class_manager` RELOAD 후에는 이미 접속 중인 플레이어도 다음 레벨업 또는 리스폰 시점에 새 스탯이 자동 적용된다.

`formula` RELOAD 후에는 즉시 적용되는 항목과 다음 이벤트 발생 시 적용되는 항목이 있다:

| 항목 | 적용 시점 |
| --- | --- |
| 크리티컬 확률/배율 | 다음 공격부터 즉시 |
| 몬스터 데미지 | 다음 몬스터 반격부터 즉시 |
| 경험치 공식 | 다음 킬부터 즉시 |
| 사망 페널티 | 다음 사망 시 |
| 리스폰 딜레이/확률 | 다음 몬스터 사망 시 |
| 몬스터 HP/코인 | 다음 리스폰 시 |
| AI 파라미터 | 다음 리스폰/분열 시 |
| 맵 크기/리젠 주기 | 즉시 (매 tick 조회) |

---

## 11. 전투 시스템

### 11.1 공격 흐름

플레이어의 공격은 두 가지 경로로 발생한다:

1. **수동 공격**: 우클릭 → 즉시 서버에 `attack` 명령 전송
2. **자동 공격**: `selectedMonster`가 설정된 상태에서 사거리 내 진입 시 클라이언트 루프가 자동 전송. 자동공격(`A키`) ON이면 타겟이 없을 때 가장 가까운 몬스터를 자동 선택

서버는 모든 공격에 대해 **거리 검증**과 **쿨다운 검증**을 수행한다 (anti-cheat).

```cpp
bool GameServer::playerAttack(Player* player, Monster* monster, int /*ignored*/) {
    if (!monster->isAlive()) return false;

    // 거리 검증
    float dist = player->pos.distanceTo(monster->pos);
    if (dist > player->attackRange) return false;

    // 쿨다운 검증 (서버 측)
    auto now = std::chrono::steady_clock::now();
    float elapsed = duration<float>(now - player->lastAttackTime).count();
    if (elapsed < 1.0f / player->attackSpeed) return false;
    player->lastAttackTime = now;

    // 크리티컬 판정 (formula.getCritical → Lua 핫픽스 가능)
    auto crit = lua_->getCritical(player);
    bool isCrit = (pct100(rng) < crit.chance);
    int actualDamage = isCrit ? player->attackPower * crit.multiplier : player->attackPower;

    monster->takeDamage(actualDamage);
    monster->aggroTargetId = player->id;  // 어그로 설정
    broadcastEvent(attack_result_json);
    // ... EXP 처리
}
```

### 11.2 공격력 (attackPower)

공격력은 클라이언트가 아닌 서버에서 관리하는 속성값이다. 클라이언트 측 `damage` 파라미터는 완전히 무시된다.

| 속성 | 기본값 | 증가 방식 |
| --- | --- | --- |
| `attackPower` | 10 | 레벨당 +2 (양 직업 동일), `class_manager.lua`에서 정의 |

레벨업 시 `applyClassStats()`가 호출되어 `attackPower`가 재계산된다.

### 11.3 크리티컬 히트 (formula.getCritical — 핫픽스 가능)

| 항목 | 기본값 | 설정 위치 |
| --- | --- | --- |
| 발동 확률 | 15% | `formula.getCritical(player).chance` |
| 데미지 배율 | 2배 | `formula.getCritical(player).multiplier` |
| 판정 방식 | `uniform_int_distribution<int>(0,99) < chance` | C++ |
| 클라이언트 표시 | `attack_result` 이벤트의 `isCrit: true` | — |

### 11.4 몬스터 어그로 시스템

**어그로 규칙**:

- 몬스터는 플레이어로부터 **공격을 받은 이후에만** 해당 플레이어를 추적한다.
- 타겟으로 선택만 해도 어그로가 발생하지 않는다.

**어그로 해제 조건**:

- 몬스터 사망 후 리스폰 (`doRespawn`)
- 어그로 대상 플레이어 사망

**추격 로직** (게임 루프 20 TPS):

```text
if (dist > monster.attackRange)
    → 플레이어 방향으로 aggroSpeed(4.0) 속도로 이동
else
    → 이동 정지, 공격 쿨다운마다 자동 반격
    → monster_attack 이벤트 브로드캐스트
```

플레이어가 공격 사거리 밖으로 다시 이동하면 즉시 추격을 재개한다.

### 11.5 몬스터 자동 반격

어그로 상태에서 공격 사거리 내에 플레이어가 있으면 게임 루프에서 자동으로 반격한다.

```cpp
// onTick 내 몬스터 반격 처리
auto now = steady_clock::now();
float elapsed = duration<float>(now - m->lastAttackTime).count();
if (elapsed >= 1.0f / m->attackSpeed) {
    m->lastAttackTime = now;
    int dmg = lua_->getMonsterDamage(m.get());  // formula.monsterDamage (핫픽스 가능)
    target->hp = max(0, target->hp - dmg);
    broadcastEvent(monster_attack_json);
}
```

### 11.6 HP 재생 (플레이어)

| 항목 | 기본값 | 설정 위치 |
| --- | --- | --- |
| 재생 주기 | 5초 | `formula.getRegenConfig().intervalSec` (핫픽스 가능) |
| 재생량 | 10 | `player->hpRegen` |
| 최대값 제한 | `maxHp` 초과 불가 | C++ |
| 처리 위치 | `onTick` 내 플레이어 루프 | — |

### 11.7 몬스터 리스폰 (formula.getRespawnConfig — 핫픽스 가능)

처치된 몬스터는 Lua 설정에 따라 리스폰된다.

```text
처치 → scheduleRespawn() → formula.getRespawnConfig() 딜레이 적용
     → respawnAt 시각 설정
onTick → warningMs 전 → respawn_soon 이벤트 브로드캐스트
       → respawnAt 도달 → doRespawn()
         → formula.respawnLevel()       레벨 변동
         → formula.monsterBaseStats()   HP/코인 산출
         → formula.monsterAI()          어그로 속도/공격속도 적용
         → 엘리트 판정 (eliteChance%)
         → 분열 판정 (splitChance%, maxMonsters 상한)
```

| 설정 | 기본값 | Lua 함수 |
| --- | --- | --- |
| 리스폰 딜레이 | 4~7초 | `getRespawnConfig().minMs/maxMs` |
| 예고 시간 | 1초 | `getRespawnConfig().warningMs` |
| 엘리트 확률 | 10% | `getRespawnConfig().eliteChance` |
| 분열 확률 | 20% | `getRespawnConfig().splitChance` |
| 인구 상한 | 9 | `getRespawnConfig().maxMonsters` |
| 홈 반경 | 20.0 | `getRespawnConfig().homeRadius` |

**리스폰 보장**: `playerKillMonster` (Lua onKill 경로 포함)에서도 `scheduleRespawn`이 호출되어 모든 경로에서 리스폰이 보장된다.

---

## 12. 성장 시스템

### 12.1 기여도 기반 경험치 분배 — formula.lua 핫픽스 가능

몬스터 처치 시 EXP는 **공격 기여도**에 따라 참여 플레이어 간 분배된다.

#### 기여도 추적 (Monster::Contribution)

| 항목 | 설명 |
| --- | --- |
| `damage` | 해당 플레이어가 가한 총 데미지 |
| `tanking` | 해당 플레이어가 받은 총 데미지 (몬스터 반격) |
| `lastTime` | 마지막 기여 시각 (만료 판정용) |

기여도는 `onTick`마다 `expireContributions(30초)`로 만료 체크된다. 30초 이상 무기여 시 해당 플레이어의 기여가 삭제된다.

#### 분배 공식 (formula.expDistribute)

```text
totalExp = formula.expReward(lastAttacker, monster)
가중치  = damage + tanking × tankingWeight(0.5)
비율    = 가중치 / 전체합 (최소 minShareRatio=10%)
파티보너스 = 참여인원별 배율 (2인 1.2x, 3인 1.5x, 4인 1.8x, 5+인 2.0x)
최종EXP = totalExp × 비율 × 파티보너스
```

솔로 처치 시 파티보너스 없이 100% 획득.

#### EXP → 레벨업

| 항목 | 기본값 | Lua 함수 |
| --- | --- | --- |
| 레벨업 기준 | `level × 100` | `formula.expToLevel(level)` |
| 다중 레벨업 | while 루프 | C++ `grantExpAndLevelUp()` |

```cpp
void GameServer::grantExpAndLevelUp(Player* player, int expGain) {
    player->exp += expGain;
    int needed = lua_->getExpToLevel(player->level);
    while (player->exp >= needed) {
        player->exp -= needed;
        player->level++;
        applyClassStats(player, *lua_);
        player->hp = player->maxHp;
        broadcastEvent(player_levelup_json);
        needed = lua_->getExpToLevel(player->level);
    }
}
```

### 12.2 레벨업 효과 (전사 기준)

| 레벨 | maxHp | attackPower | 레벨업 기준 EXP |
| --- | --- | --- | --- |
| 1 | 100 | 10 | 100 |
| 2 | 120 | 12 | 200 |
| 3 | 140 | 14 | 300 |
| N | 100 + (N-1)×20 | 10 + (N-1)×2 | N×100 |

레벨업 시 HP가 새 `maxHp`로 완전 회복된다. 스탯 공식은 `class_manager.lua`에서 정의하며 핫픽스 가능하다.

### 12.3 플레이어 사망 및 리스폰

HP가 0 이하가 되면 `onTick`에서 감지하여 처리한다.

```cpp
if (!target->isAlive()) {
    broadcastEvent(player_death_json);

    // 사망 페널티 (formula.deathPenalty — 핫픽스 가능)
    auto penalty = lua_->getDeathPenalty(target);
    if (penalty.resetLevel >= 0) target->level = penalty.resetLevel;
    if (penalty.resetExp   >= 0) target->exp   = penalty.resetExp;
    applyClassStats(target, *lua_);  // 직업별 스탯으로 복원
    target->hp = target->maxHp;

    // 랜덤 위치 리스폰 (formula.getMapConfig 범위)
    auto mapCfg = lua_->getMapConfig();
    target->pos = randomPos(mapCfg.width, mapCfg.height);
    target->targetPos = target->pos;

    // 타이머 초기화 + 어그로 해제
    // ...
}
```

사망 페널티 기본값: `resetLevel = math.ceil(level/2), resetExp = 0`. 예: 레벨 9→5, 8→4, 7→4. `-1`로 설정하면 해당 값을 유지한다.

클라이언트는 `player_death` 이벤트 수신 시 사망 오버레이("사망 패널티 적용")를 3초간 표시하고, `selectedMonster`, `manualMoveTarget`, `chaseInRange` 등 전투 상태를 모두 초기화한다. 리스폰 후 자동공격이 ON이면 새 위치 기준으로 가장 가까운 몬스터를 자동 선택한다.

---

## 13. 인벤토리·아이템·상점 시스템

### 13.1 인벤토리

플레이어는 `std::map<int,int>` 형태의 인벤토리를 보유한다. 키=아이템ID, 값=수량.

```cpp
// Player.h
std::map<int, int> inventory;
void addItem(int itemId, int count = 1);
bool removeItem(int itemId, int count = 1);  // 수량 부족 시 false
int  getItemCount(int itemId) const;
```

인벤토리는 `getStateJson()`에서 아이템 이름과 함께 클라이언트에 전송된다.

### 13.2 아이템 (체력물약)

| ID | 이름 | 가격 | 효과 | Lua 파일 |
| --- | --- | --- | --- | --- |
| 1000 | 소형 체력물약 | 100코인 | HP +50 | `item/1000.lua` |
| 1001 | 중형 체력물약 | 300코인 | HP +100 | `item/1001.lua` |
| 1002 | 대형 체력물약 | 500코인 | HP +150 | `item/1002.lua` |

아이템 스크립트 구조:

```lua
local item = { id = 1000, name = "소형 체력물약", price = 100, heal = 50 }

function item.canUse(owner)   return true end
function item.useItem(owner)  owner:addHP(item.heal, item.name) end
function item.getInfo()       return { id=item.id, name=item.name, price=item.price, desc="HP "..item.heal.." 회복" } end

item_manager.register(item)
```

`ITEM USE` 처리 흐름: 인벤토리 보유 확인 → `canUseItem()` → `removeItem()` → `useItem()` → `item_used` 이벤트 broadcast.

### 13.3 몬스터 아이템 드롭

몬스터 처치 시 `formula.getDropTable(monster)`에서 드롭 테이블을 조회하고, 확률에 따라 바닥 아이템(GroundItem)을 생성한다.

```lua
function formula.getDropTable(monster)
    local drops = {
        { itemId = 1000, chance = 15, minQty = 1, maxQty = 1 },  -- 소형 15%
        { itemId = 1001, chance = 8,  minQty = 1, maxQty = 1 },  -- 중형 8%
        { itemId = 1002, chance = 3,  minQty = 1, maxQty = 1 },  -- 대형 3%
    }
    if monster.isElite then  -- 엘리트 몬스터: 2배 확률
        for _, d in ipairs(drops) do d.chance = d.chance * 2 end
    end
    return drops
end
```

#### GroundItem (바닥 아이템)

```cpp
struct GroundItem {
    int id;       // 고유 ID (nextGroundItemId_++)
    int itemId;   // 아이템 종류
    int qty;      // 수량
    Vec2 pos;     // 드롭 위치 (몬스터 사망 위치)
    std::chrono::steady_clock::time_point dropTime;
};
```

| 설정 | 값 |
| --- | --- |
| 만료 시간 | 60초 (`GROUND_ITEM_EXPIRE_SEC`) |
| 줍기 거리 | 3.0 유닛 (`PICKUP_RANGE`) |
| 만료 시 | `item_expire` 이벤트 broadcast 후 삭제 |

클라이언트에서 바닥 아이템은 황금색 맥동 원(pulsing circle)과 아이템 이름 라벨로 표시된다.

### 13.4 상점 시스템

`SHOP LIST` → `item_manager.getShopList()`에서 등록된 모든 아이템 정보를 반환. **요청한 플레이어에게만** JSON 응답 (broadcast 아님).

`SHOP BUY` → 코인 확인 → `player->addCoin(-cost)` → `player->addItem(itemId, qty)` → `shop_buy` 이벤트 broadcast.

---

## 14. 직업 시스템

### 14.1 직업 종류

접속 시 두 직업 중 하나를 선택한다. 직업은 플레이어 사망 후 리스폰 시에도 유지된다 (레벨은 ceil(level/2)로 감소).

| 직업 | job 값 | maxHp (lv1) | attackSpeed | attackRange | attackPower (lv1) |
| --- | --- | --- | --- | --- | --- |
| 전사 | `"warrior"` | 100 | 1.0/sec | 5.0 units | 10 |
| 궁수 | `"archer"` | 80 | 0.8/sec | 20.0 units | 10 |

### 14.2 스탯 정의 (class_manager.lua)

직업별 스탯값은 C++ 코드가 아닌 Lua 파일에서 관리된다. 서버 재시작 없이 `RELOAD class_manager`로 즉시 반영할 수 있다.

```lua
-- lua/EventManager/class_manager.lua
function class_manager.getStats(job, level)
    local base   = 100 + (level - 1) * 20
    local atkPow = 10  + (level - 1) * 2

    if job == "archer" then
        return {
            maxHp       = math.floor(base * 0.8),
            attackPower = atkPow,
            attackSpeed = 0.8,
            attackRange = 20.0,
        }
    else  -- warrior (default)
        return {
            maxHp       = base,
            attackPower = atkPow,
            attackSpeed = 1.0,
            attackRange = 5.0,
        }
    end
end
```

### 14.3 C++ 연동 (LuaEngine::getClassStats)

```cpp
// LuaEngine.h
struct ClassStats {
    int   maxHp;
    int   attackPower;
    float attackSpeed;
    float attackRange;
};
ClassStats getClassStats(Player* player);

// LuaEngine.cpp
LuaEngine::ClassStats LuaEngine::getClassStats(Player* p) {
    ClassStats def{ 100, 10, 1.0f, 5.0f };  // fallback (class_manager 없을 때)
    sol::protected_function f = lua["class_manager"]["getStats"];
    if (!f.valid()) return def;
    auto res = f(p->job, p->level);
    // ... 테이블에서 각 필드 추출 후 반환
}
```

### 14.4 스탯 적용 시점

`applyClassStats(Player*, LuaEngine&)`는 다음 세 시점에 호출된다:

| 시점 | 설명 |
| --- | --- |
| 플레이어 생성 (`createPlayer`) | 초기 직업 스탯 설정 |
| 레벨업 (`playerAttack` 내 EXP 처리) | 새 레벨에 맞는 스탯 재계산 |
| 사망 리스폰 (`onTick` 내 사망 처리) | 감소된 레벨 기준 직업 스탯으로 초기화 |

### 14.5 핫픽스 절차

1. `lua/EventManager/class_manager.lua` 수치 수정
2. 게임 서버 콘솔(telnet/웹)에서 `RELOAD class_manager` 입력
3. 서버 응답: `OK: Reloaded class_manager`
4. 이후 레벨업 또는 리스폰하는 플레이어부터 새 스탯 적용

---

## 15. 클라이언트 자동 행동

### 17.1 자동 물약 사용

HP가 50% 미만이고 체력물약을 보유하고 있으면 **작은 물약부터** 자동 사용한다.

| 조건 | 동작 |
| --- | --- |
| HP < 50% + 물약 보유 + 쿨다운 만료 | 가장 작은 물약(1000→1001→1002) 사용 |
| 쿨다운 | 5초 (모든 체력물약 공유, `POTION_COOLDOWN_MS=5000`) |

쿨다운은 `item_used` 이벤트 수신 시 갱신된다. 인벤토리 패널에서 잔여 쿨다운을 초 단위로 표시한다.

### 17.2 아이템 줍기 vs 몬스터 공격 선택

타겟 몬스터 사망 후 자동공격 상태에서 주변 아이템과 몬스터를 비교하여 행동을 결정한다.

```text
아이템이 가장 가까움 → 100% 아이템 줍기
같은 거리            → 35% 아이템 줍기, 65% 몬스터 공격
몬스터가 가장 가까움 → 100% 몬스터 공격
줍기 거리(3.0) 이내  → 즉시 PICKUP 전송
```

바닥 클릭으로 아이템 쪽을 클릭하면 이동 후 자동 줍기(`pendingPickup`). 도착 시 PICKUP 명령 자동 전송.

### 17.3 인벤토리 UI 최적화

인벤토리 패널은 매 tick(50ms) 갱신되지만, **변경 감지**(JSON.stringify 비교)로 DOM이 실제로 바뀔 때만 innerHTML을 교체한다. 이를 통해 USE 버튼 클릭이 tick에 의해 중단되는 문제를 방지한다.

---

## 16. 클래스 레퍼런스

### Vec2

```cpp
struct Vec2 {
    float x, y;
    float distanceTo(const Vec2& o) const;
    std::string toString() const;  // "(x.x, y.y)"
};
```

### Map

```cpp
class Map {
    static constexpr float WIDTH  = 100.f;
    static constexpr float HEIGHT = 100.f;
    static bool isInBounds(float x, float y);
};
```

### Entity (베이스 클래스)

```cpp
class Entity {
public:
    int         id;
    std::string name;
    std::string type;   // "Player" or "Monster"
    int         level;
    int         hp, maxHp;
    int         coin;
    float       speed;
    Vec2        pos;
    Vec2        targetPos;

    // 전투 속성 (직업/레벨에 따라 applyClassStats로 설정)
    float       attackRange = 5.0f;   // 공격 사거리 (units)
    float       attackSpeed = 1.0f;   // 초당 공격 횟수
    int         attackPower = 10;     // 공격력 (서버 권위, 클라이언트 값 무시)
    int         hpRegen     = 0;      // HP 재생량 (5초 주기, 플레이어만)

    std::chrono::steady_clock::time_point lastAttackTime;

    bool isAlive() const { return hp > 0; }
    bool stepTowardTarget(float dt);  // targetPos 방향으로 dt초 이동
};
```

### Player

```cpp
class Player : public Entity {
public:
    int         gender;              // 0=male, 1=female
    std::string job = "warrior";     // "warrior" | "archer"
    int         exp = 0;             // 누적 경험치

    std::map<int, std::map<int, int>> questData;  // questId → paramId → value
    std::map<int, int> inventory;                // itemId → count

    std::chrono::steady_clock::time_point lastRegenTime;  // 마지막 HP 재생 시각

    Player(int id, const std::string& name, int level = 1, int gender = 0);
    //   생성자: hpRegen = 10, lastRegenTime = now()

    void addCoin(int amount, const std::string& source);
    void addHP(int amount, const std::string& source);
    void addItem(int itemId, int count = 1);
    bool removeItem(int itemId, int count = 1);
    int  getItemCount(int itemId) const;
    void setQuestData(int questId, int paramId, int value);
    void addQuestData(int questId, int paramId, int value);
    int  getQuestData(int questId, int paramId) const;
    void removeQuestData(int questId);
    void printStatus() const;
};
```

### Monster

```cpp
class Monster : public Entity {
public:
    std::string baseName;
    int         baseCoin;
    int         baseLevel;

    int         aggroTargetId = -1;   // 추적 중인 플레이어 ID (-1=없음)
    float       aggroSpeed    = 4.0f; // 추적 속도 (units/sec)

    // 기여도 추적 (EXP 분배용)
    struct Contribution { int damage=0; int tanking=0; time_point lastTime; };
    std::map<int, Contribution> contributions;  // playerId → 기여도
    void addDamageContribution(int playerId, int damage);
    void addTankingContribution(int playerId, int damage);
    void expireContributions(float expireSec);
    void clearContributions();

    // 생성자: attackSpeed = 0.8f (초당 0.8회)

    void takeDamage(int dmg);
};
```

### LuaEngine

```cpp
class LuaEngine {
    bool initialize(const std::string& scriptDir);
    bool loadScript(const std::string& moduleName);
    bool reloadScript(const std::string& moduleName);

    void registerPlayer(Player*);
    void unregisterPlayer(Player*);
    void registerMonster(Monster*);
    void unregisterMonster(Monster*);

    void publishEvent(Player* owner, const std::string& eventName);
    void publishEvent(Player* owner, const std::string& eventName, Monster* target);
    void publishEvent(Player* owner, const std::string& eventName, Player* target);

    // 직업 스탯 조회 (class_manager.getStats 호출)
    struct ClassStats { int maxHp, attackPower; float attackSpeed, attackRange; };
    ClassStats getClassStats(Player* player);

    // formula.lua 공식 조회 (RELOAD formula 핫픽스 가능)
    struct CriticalInfo { int chance; int multiplier; };
    CriticalInfo getCritical(Player*);
    int  getMonsterDamage(Monster*);
    int  getExpReward(Player*, Monster*);
    int  getExpToLevel(int level);
    struct DeathPenalty { int resetLevel; int resetExp; };
    DeathPenalty getDeathPenalty(Player*);

    struct SpawnEntry { int id; std::string name; int level; float x, y; };
    std::vector<SpawnEntry> getSpawnTable();
    struct RespawnConfig { int minMs, maxMs, warningMs, eliteChance, splitChance, maxMonsters; float homeRadius; };
    RespawnConfig getRespawnConfig();
    int  getRespawnLevel(int baseLevel, int currentLevel);
    struct MonsterBaseStats { int maxHp; int coin; };
    MonsterBaseStats getMonsterBaseStats(int level, bool isElite);
    struct MonsterAI { float aggroSpeed; float attackSpeed; };
    MonsterAI getMonsterAI(Monster*);
    struct MapConfig { float width; float height; };
    MapConfig getMapConfig();
    struct RegenConfig { float intervalSec; };
    RegenConfig getRegenConfig();
    struct TickConfig { int tickMs; };
    TickConfig getTickConfig();

    // 기여도 기반 EXP 분배
    struct ContributionConfig { float expireSec; float tankingWeight; float minShareRatio; std::map<int,float> partyBonus; };
    struct ContributionEntry { int playerId; int damage; int tanking; float ratio; };
    struct ExpShareResult { int playerId; int exp; };
    ContributionConfig getContributionConfig();
    std::vector<ExpShareResult> getExpDistribute(int totalExp, const std::vector<ContributionEntry>&, const ContributionConfig&);

    // 아이템 드롭/상점
    struct DropEntry { int itemId; int chance; int minQty; int maxQty; };
    std::vector<DropEntry> getDropTable(Monster*);
    struct ShopItem { int id; std::string name; int price; std::string desc; };
    std::vector<ShopItem> getShopList();
    ShopItem getItemInfo(int itemId);

    bool applyBuff(Player*, int buffId);
    bool removeBuff(Player*, int buffId);
    bool canUseItem(Player*, int itemId);
    void useItem(Player*, int itemId);
    bool acceptQuest(Player*, int questId);
    bool completeQuest(Player*, int questId);
    bool removeQuest(Player*, int questId);
};
```

### GameServer

```cpp
class GameServer {
    bool initialize(const std::string& scriptDir, int port = 7000);
    void run();   // blocking (TCP + 20 TPS 게임 루프)
    void stop();

    // 게임 액션 (내부에서 gameMutex_ 획득)
    bool playerAttack(Player*, Monster*, int damage = 10);  // damage 파라미터 무시
    void playerStruck(Player*, Monster*);
    void playerKillMonster(Player*, Monster*);
    void playerApplyBuff(Player*, int buffId);
    void playerRemoveBuff(Player*, int buffId);
    void playerUseItem(Player*, int itemId);
    void playerAcceptQuest(Player*, int questId);
    void playerCompleteQuest(Player*, int questId);
    void playerRemoveQuest(Player*, int questId);

    // 객체 관리
    Player*  createPlayer(int id, const std::string& name,
                          int level = 1, int gender = 0,
                          const std::string& job = "warrior");
    Monster* createMonster(int id, const std::string& name,
                           int level = 1, float x = -1.f, float y = -1.f);
    void     spawnMonstersFromLua();  // formula.getSpawnTable() 에서 초기 몬스터 생성
    Player*  getPlayer(int id);
    Monster* getMonster(int id);
    void     removePlayer(int id);

    // WebSocket 연동
    void setWsServer(WsServer* ws);
    void broadcastToOthers(int excludeId);         // 상태 JSON 동기화
    void broadcastAll();                            // tick 완료 후 전체 전송
    void broadcastEvent(const std::string& json);  // 이벤트 전체 전파

    std::string processCommand(int playerId, const std::string& line);
    std::string getStateJson(int playerId);  // inventory, groundItems 포함

private:
    // 바닥 아이템
    std::map<int, GroundItem> groundItems_;
    int nextGroundItemId_ = 1;
    static constexpr float GROUND_ITEM_EXPIRE_SEC = 60.0f;
    static constexpr float PICKUP_RANGE = 3.0f;

    void onTick();          // 20 TPS 게임 루프 (HP재생, 어그로, 반격, 사망처리, 아이템만료)
    void scheduleRespawn(Monster*);
    bool doRespawn(Monster*);
    void sendRespawnWarning(Monster*);
    void splitSpawn(Monster* parent);
    void distributeExp(Monster*);           // 기여도 기반 EXP 분배
    void grantExpAndLevelUp(Player*, int);  // EXP 부여 + 레벨업 처리
    void dropItemsFromMonster(Monster*);    // 드롭 테이블 기반 아이템 생성
};
```

### WsSession

```cpp
class WsSession : public std::enable_shared_from_this<WsSession> {
    // RFC 6455 프레임 파싱 (헤더 → 확장 길이 → payload+mask)
    // JSON 명령 → processCommand() 변환 (connect 시 job 필드 포함)
    // 명령 처리 후 자동 상태 push + broadcast 트리거
    // onDisconnect(): 세션 제거 + removePlayer() + broadcastToOthers(-1)

    void pushJson(const std::string& json);  // thread-safe, asio::post 사용
    int  playerId() const;
};
```

### WsServer

```cpp
class WsServer {
    WsServer(GameServer& gs, int port = 7001);
    void start();  // 별도 스레드에서 ioc.run()
    void stop();

    void addSession(int playerId, std::shared_ptr<WsSession>);
    void removeSession(int playerId);

    // excludeId = -1: 모두에게 / -2: 모두에게 동일 JSON
    void broadcastExcept(int excludeId, std::function<std::string(int)> stateGetter);
};
```

---

## 17. 테스트 세션 예시

### 17.1 웹 클라이언트 사용

1. `http://localhost:7001` 접속
2. 이름 입력 후 직업(⚔ 전사 / 🏹 궁수) 선택 → **접속** 버튼
3. 지도 빈 곳 클릭 → 해당 위치로 이동 (현재 타겟 자동 해제)
4. 몬스터 클릭 → 타겟 선택 및 자동 추적 시작. 사거리 밖이면 접근 후 공격, 사거리 내이면 즉시 공격
5. **A키**로 자동공격 ON → 타겟 없을 때 가장 가까운 몬스터 자동 선택. 이동 명령 후에는 목적지 도착 시까지 자동 선택 억제
6. **Escape** → 타겟 해제 (자동공격 ON이면 이후 자동 재선택)
7. 몬스터 처치 → 기여도 기반 EXP 분배, 레벨업 시 HP 바 증가 + 공격력 상승
8. 몬스터 처치 시 확률적으로 바닥에 체력물약 드롭 → 클릭하여 줍기
9. 상점 버튼 → 아이템 목록 → 코인으로 구매 → 인벤토리에서 USE 클릭
10. HP < 50% 시 자동 물약 사용 (5초 공유 쿨다운)
11. 사망 시 사망 오버레이 표시 → 레벨 절반으로 자동 리스폰 (직업 유지)
12. 다른 브라우저 탭에서 동일 URL 접속 → 실시간으로 서로의 위치와 몬스터 HP 변화 확인
13. 같은 몬스터를 여러 플레이어가 공격 시 각자 슬롯 배분, 화면에서 겹침 없이 표시

### 17.2 telnet 수동 테스트

```text
telnet 127.0.0.1 7000

GameServer v1.0 -- type CONNECT <name> to begin

CONNECT hero level:1 gender:0 job:archer
OK: Connected as hero (lv=1, archer)

MOVE 20 20
OK: Moved to (20.0, 20.0)

LOOK
You are at (20.0, 20.0)
  Monster:Goblin(id=2000) @ (20.0, 20.0)  dist=0.0  hp=100/100 [IN RANGE]
  Monster:Orc(id=2001)    @ (50.0, 50.0)  dist=42.4 hp=100/100
  Monster:Dark Knight(id=2002) @ (80.0, 80.0)  dist=84.9 hp=100/100

ATTACK 2000
OK: Attacked Goblin (hp=90, dmg=10)

STATUS
Player:hero lv=1 job=archer hp=80/80 atk=10 range=20 coin=0 exp=0/100

RELOAD class_manager
OK: Reloaded class_manager

QUIT
BYE
```

### 17.3 핫픽스 예시

#### 직업 스탯 핫픽스

```text
1. lua/EventManager/class_manager.lua 수정
   (예: 궁수 attackRange = 20.0 → 15.0)

2. telnet 또는 웹 클라이언트에서:
   RELOAD class_manager
   → OK: Reloaded class_manager

3. 다음 레벨업 또는 리스폰부터 새 스탯 적용
```

#### 전투·경험치·스폰 공식 핫픽스

```text
1. lua/EventManager/formula.lua 수정 예시:

   -- 크리티컬 확률 15% → 25%, 배율 2x → 3x
   function formula.getCritical(player)
       return { chance = 25, multiplier = 3 }
   end

   -- 사망 페널티: 기본은 ceil(level/2), 아래는 레벨 유지 예시
   function formula.deathPenalty(player)
       return { resetLevel = -1, resetExp = 0 }  -- 레벨 유지, EXP만 리셋
   end

   -- 엘리트 확률 30%, 인구 상한 15
   function formula.getRespawnConfig()
       return { minMs=4000, maxMs=7000, warningMs=1000,
                eliteChance=30, splitChance=20, maxMonsters=15, homeRadius=25.0 }
   end

2. RELOAD formula
   → OK: Reloaded formula

3. 즉시 반영 (다음 공격/킬/리스폰부터 새 공식 적용)
```

### 17.4 멀티플레이어 동시 접속

- 브라우저 여러 탭 또는 telnet과 브라우저 혼용 모두 가능
- WS 클라이언트끼리는 실시간 broadcast 수신 (이동/공격/반격/레벨업/사망/리스폰)
- TCP(telnet) 클라이언트는 broadcast를 받지 않음 (요청-응답만)
- 모든 클라이언트가 동일한 `GameServer` 게임 상태를 공유

---

마지막 업데이트: 2026-03-16
