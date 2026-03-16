#pragma once
#include <memory>
#include <string>
#include <deque>
#include <thread>
#include <atomic>
#include <mutex>
#include <map>
#include <functional>
#include <asio.hpp>

class GameServer;
class WsServer;  // forward decl for WsSession

// ── Per-client WebSocket session ────────────────────────────────────
class WsSession : public std::enable_shared_from_this<WsSession> {
public:
    WsSession(asio::ip::tcp::socket socket, GameServer& gs, WsServer& ws);
    void start();

    // Thread-safe: push JSON to this client from any thread
    void pushJson(const std::string& json);

    int playerId() const { return playerId_; }

private:
    // HTTP phase
    void doHttpRead();
    void routeHttp(const std::string& headers);
    void serveHtml();
    void doWsHandshake(const std::string& wsKey);

    // WebSocket frame read state machine
    void wsReadHeader();
    void wsReadExtLen(uint8_t opcode, bool masked, int extBytes);
    void wsReadPayload(uint8_t opcode, bool masked, uint64_t plen);
    void wsHandleText(const std::string& text);
    void onDisconnect();

    // WebSocket write
    void wsSend(const std::string& jsonText);   // encode as WS text frame
    void wsSendRaw(std::string data);            // enqueue raw bytes
    void flushWrite();

    asio::ip::tcp::socket   socket_;
    asio::streambuf         readBuf_;
    GameServer&             gs_;
    WsServer&               ws_;
    int                     playerId_;

    // WS frame parsing buffers
    uint8_t hdr_[2]    = {};
    uint8_t extBuf_[8] = {};

    std::deque<std::string> writeQueue_;
    bool                    writing_ = false;

    static std::atomic<int> nextPlayerId_;
};

// ── HTTP+WebSocket server (runs on its own thread) ──────────────────
class WsServer {
public:
    WsServer(GameServer& gs, int port = 7001);
    ~WsServer();

    void start();
    void stop();

    // Session registry — called by WsSession on connect/disconnect
    void addSession(int playerId, std::shared_ptr<WsSession> session);
    void removeSession(int playerId);

    // Broadcast state to all sessions except excludeId.
    // stateGetter(playerId) returns the JSON string for that player's view.
    // Pass excludeId = -1 to broadcast to everyone.
    void broadcastExcept(int excludeId, std::function<std::string(int)> stateGetter);

private:
    GameServer&             gs_;
    int                     port_;
    asio::io_context        ioc_;
    asio::ip::tcp::acceptor acceptor_;
    std::thread             thread_;

    std::mutex                              sessionsMutex_;
    std::map<int, std::weak_ptr<WsSession>> sessions_;

    void doAccept();
};
