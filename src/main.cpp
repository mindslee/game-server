#include "GameServer.h"
#include "WsServer.h"
#include <iostream>
#include <string>
#include <filesystem>

int main(int argc, char* argv[]) {
    // Find lua/EventManager directory
    std::string scriptDir;
    for (const char* p : {"lua/EventManager", "../lua/EventManager", "../../lua/EventManager"}) {
        if (std::filesystem::exists(p)) {
            scriptDir = std::filesystem::absolute(p).string();
            break;
        }
    }
    if (scriptDir.empty()) {
        std::cerr << "Cannot find lua/EventManager directory\n";
        return 1;
    }

    int port   = 7000;
    int wsPort = 7001;
    if (argc >= 2) port   = std::stoi(argv[1]);
    if (argc >= 3) wsPort = std::stoi(argv[2]);

    GameServer server;
    if (!server.initialize(scriptDir, port)) {
        std::cerr << "Server initialization failed\n";
        return 1;
    }

    // Lua formula.getSpawnTable() 에서 초기 몬스터 스폰 (RELOAD formula 핫픽스 가능)
    server.spawnMonstersFromLua();

    // Verify Lua integration
    server.runTestScenario();

    // Start HTTP+WebSocket server on wsPort (non-blocking, own thread)
    WsServer wsServer(server, wsPort);
    wsServer.start();
    server.setWsServer(&wsServer);  // enable real-time broadcast
    std::cout << "Web client: http://localhost:" << wsPort << "\n";

    // Start TCP game server (blocking)
    std::cout << "Press Ctrl+C to stop.\n";
    server.run();

    wsServer.stop();
    return 0;
}
