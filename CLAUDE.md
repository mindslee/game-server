# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Build & Run

```bash
# Build (from project root)
cmake -S . -B build                          # first time only
cmake --build build --config Release         # incremental build

# Run (from build/Release)
cd build/Release && GameServer.exe           # TCP:7000, WS:7001
GameServer.exe 8000 8001                     # custom ports

# Web client
# http://localhost:7001
```

**Post-build copy**: CMakeLists.txt has a `POST_BUILD` rule that copies `lua/` and `web/` to the output directory on every build. This means source `lua/` and `web/` are the authoritative copies — edits to `build/Release/lua/` or `build/Release/web/` will be overwritten on next build.

**After building, always restart the server** (kill existing process, then launch new one) without waiting for the user to ask.

## Lua Hot-Reload (no rebuild needed)

Edit the source `lua/EventManager/*.lua` file, copy it to `build/Release/lua/EventManager/`, then send `RELOAD <module>` via TCP or WebSocket. For web/index.html changes, just copy to `build/Release/web/` and refresh the browser.

## Architecture

**Two-server, one game state**: A TCP text server (port 7000) and an HTTP+WebSocket server (port 7001) share a single `GameServer` instance protected by `gameMutex_` (recursive_mutex). The WsServer runs on a dedicated thread.

**Game loop**: 20 TPS via asio steady_timer in `GameServer::onTick()`. Handles movement interpolation, HP regen, monster aggro/attacks, player death, and respawn queues.

**Entity hierarchy**: `Entity` (base: id, pos, targetPos, speed, hp, attackRange, attackSpeed, attackPower) → `Player` (job, EXP, questData, hpRegen) and `Monster` (aggro, respawn timer, elite flag).

**Lua integration**: `LuaEngine` wraps sol2. Seven manager modules load in order: event → item → buff → quest → monster → player → class. Individual scripts (buff/1000, quest/1000, player/base) register via their manager. `event_manager` is a pub/sub bus — C++ calls `publishEvent()` which dispatches to Lua listeners (onAttack, onKill, onStruck, etc.).

**Persistent Lua objects**: `LuaEngine` caches `sol::object` per player/monster ID so `event_manager` always sees the same Lua table key for a given entity.

**Client combat model**: `selectedMonster` is the single source of truth for targeting. When set (manually or by autoAttack), the client chases the monster and attacks when in range. `manualMoveTarget` suppresses auto-targeting until the player arrives at their clicked destination.

## Key Conventions

- **Server-authoritative combat**: Client `damage` parameter is ignored. Server uses `player->attackPower` with 15% crit chance (2x). All range and cooldown checks are server-side.
- **Job stats in Lua**: `class_manager.getStats(job, level)` returns `{maxHp, attackPower, attackSpeed, attackRange}`. Applied via `applyClassStats()` at player creation, level-up, and death respawn.
- **WebSocket JSON ↔ TCP text**: `WsSession::wsHandleText()` translates JSON commands to TCP-format strings before calling `processCommand()`. Same game logic for both protocols.
- **Broadcast patterns**: `broadcastToOthers(excludeId)` sends per-player state JSON; `broadcastEvent(json)` sends identical event JSON to all clients.
- **Lua hot-reload safety**: Manager scripts must preserve previous state (e.g., `local scripts = mgr and mgr.__scripts or {}`). Individual scripts re-register via their manager which removes the old listener first.

## File Roles (non-obvious)

| File | Role |
|------|------|
| `src/WsServer.cpp` | Hand-rolled RFC 6455 WebSocket + HTTP server (no library). JSON→text command translation lives here. |
| `src/sha1.h` | WebSocket handshake `Sec-WebSocket-Accept` key generation |
| `src/Map.h` | Only holds constants (`WIDTH=100`, `HEIGHT=100`). `Map::ATTACK_RANGE` is legacy — per-player `attackRange` is authoritative. |
| `web/index.html` | 50KB single-file client: login screen + Canvas game + sidebar + log. All game rendering and client-side combat logic. |
| `lua/EventManager/class_manager.lua` | Hotfixable job stats. RELOAD takes effect on next level-up or respawn. |
| `docs.md` | Comprehensive design document (Korean). Kept up-to-date with code changes. |

## Language

The user communicates in Korean. All explanations, comments, and documentation should be in Korean. Technical terms and code identifiers stay in English.
