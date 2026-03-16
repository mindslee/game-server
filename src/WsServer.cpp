#include "WsServer.h"
#include "GameServer.h"
#include "sha1.h"

#include <nlohmann/json.hpp>
#include <iostream>
#include <sstream>
#include <fstream>
#include <algorithm>
#include <cstring>

using json = nlohmann::json;

// ================================================================
// WsSession
// ================================================================

std::atomic<int> WsSession::nextPlayerId_{5000};

WsSession::WsSession(asio::ip::tcp::socket socket, GameServer& gs, WsServer& ws)
    : socket_(std::move(socket))
    , gs_(gs)
    , ws_(ws)
    , playerId_(nextPlayerId_.fetch_add(1))
{}

void WsSession::start() { doHttpRead(); }

// ──── HTTP Phase ──────────────────────────────────────────────────

void WsSession::doHttpRead() {
    auto self = shared_from_this();
    asio::async_read_until(socket_, readBuf_, "\r\n\r\n",
        [this, self](std::error_code ec, size_t) {
            if (ec) return;
            std::istream is(&readBuf_);
            std::string headers(std::istreambuf_iterator<char>(is), {});
            routeHttp(headers);
        });
}

void WsSession::routeHttp(const std::string& headers) {
    std::string lower = headers;
    std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);

    bool isWs = lower.find("upgrade: websocket") != std::string::npos;
    if (!isWs) { serveHtml(); return; }

    // Extract Sec-WebSocket-Key (case-insensitive search)
    std::string wsKey;
    size_t pos = lower.find("sec-websocket-key:");
    if (pos != std::string::npos) {
        size_t colon = headers.find(':', pos) + 1;
        size_t eol   = headers.find('\r', colon);
        wsKey = headers.substr(colon, eol - colon);
        wsKey.erase(0, wsKey.find_first_not_of(" \t"));
        wsKey.erase(wsKey.find_last_not_of(" \t\r\n") + 1);
    }

    doWsHandshake(wsKey);
}

void WsSession::serveHtml() {
    std::string body;
    for (const char* p : {"web/index.html", "../web/index.html", "../../web/index.html"}) {
        std::ifstream f(p, std::ios::binary);
        if (f) { body.assign(std::istreambuf_iterator<char>(f), {}); break; }
    }
    if (body.empty())
        body = "<html><body><h2>web/index.html not found</h2>"
               "<p>Run the server from its output directory.</p></body></html>";

    auto self = shared_from_this();
    std::string resp =
        "HTTP/1.1 200 OK\r\n"
        "Content-Type: text/html; charset=utf-8\r\n"
        "Content-Length: " + std::to_string(body.size()) + "\r\n"
        "Connection: close\r\n\r\n" + body;
    wsSendRaw(std::move(resp));
}

void WsSession::doWsHandshake(const std::string& wsKey) {
    std::string accept = sha1::wsAcceptKey(wsKey);
    std::string resp =
        "HTTP/1.1 101 Switching Protocols\r\n"
        "Upgrade: websocket\r\n"
        "Connection: Upgrade\r\n"
        "Sec-WebSocket-Accept: " + accept + "\r\n\r\n";

    auto self = shared_from_this();
    auto buf  = std::make_shared<std::string>(std::move(resp));
    asio::async_write(socket_, asio::buffer(*buf),
        [this, self, buf](std::error_code ec, size_t) {
            if (ec) return;
            // Register session, push initial full state, then start reading frames
            ws_.addSession(playerId_, shared_from_this());
            wsSend(gs_.getStateJson(playerId_));
            wsReadHeader();
        });
}

// ──── WebSocket Frame Read ─────────────────────────────────────────

void WsSession::wsReadHeader() {
    auto self = shared_from_this();
    asio::async_read(socket_, asio::buffer(hdr_, 2),
        [this, self](std::error_code ec, size_t) {
            if (ec) { onDisconnect(); return; }
            uint8_t opcode  = hdr_[0] & 0x0F;
            bool    masked  = (hdr_[1] & 0x80) != 0;
            uint8_t lenByte = hdr_[1] & 0x7F;

            if (opcode == 0x8) { onDisconnect(); return; }  // Close
            if (opcode == 0x9) {                             // Ping → Pong
                std::string pong; pong.push_back(0x8A); pong.push_back(0);
                wsSendRaw(std::move(pong));
                wsReadHeader(); return;
            }
            if (lenByte < 126)
                wsReadPayload(opcode, masked, lenByte);
            else
                wsReadExtLen(opcode, masked, lenByte == 126 ? 2 : 8);
        });
}

void WsSession::wsReadExtLen(uint8_t opcode, bool masked, int extBytes) {
    auto self = shared_from_this();
    asio::async_read(socket_, asio::buffer(extBuf_, extBytes),
        [this, self, opcode, masked, extBytes](std::error_code ec, size_t) {
            if (ec) { onDisconnect(); return; }
            uint64_t plen = 0;
            for (int i = 0; i < extBytes; i++) plen = (plen << 8) | extBuf_[i];
            if (plen > 65536) { onDisconnect(); return; }
            wsReadPayload(opcode, masked, plen);
        });
}

void WsSession::wsReadPayload(uint8_t opcode, bool masked, uint64_t plen) {
    auto self = shared_from_this();
    size_t toRead = (size_t)plen + (masked ? 4 : 0);

    auto buf = std::make_shared<std::vector<uint8_t>>(toRead);
    asio::async_read(socket_, asio::buffer(*buf),
        [this, self, buf, opcode, masked, plen](std::error_code ec, size_t) {
            if (ec) { onDisconnect(); return; }
            const uint8_t* maskKey = masked ? buf->data()     : nullptr;
            const uint8_t* raw     = masked ? buf->data() + 4 : buf->data();
            std::string text(plen, '\0');
            for (size_t i = 0; i < plen; i++)
                text[i] = raw[i] ^ (masked ? maskKey[i % 4] : 0);

            if (opcode == 0x1) wsHandleText(text);
            wsReadHeader();
        });
}

// ──── Disconnect cleanup ───────────────────────────────────────────

void WsSession::onDisconnect() {
    ws_.removeSession(playerId_);
    gs_.removePlayer(playerId_);
    // Notify remaining clients that a player left
    gs_.broadcastToOthers(-1);
}

// ──── JSON Dispatch ────────────────────────────────────────────────

void WsSession::wsHandleText(const std::string& text) {
    auto j = json::parse(text, nullptr, false);
    if (j.is_discarded()) {
        wsSend(json{{"type","error"},{"msg","Invalid JSON"}}.dump());
        wsSend(gs_.getStateJson(playerId_));
        return;
    }

    std::string cmd = j.value("cmd", "");
    std::string line;

    if (cmd == "connect") {
        line = "CONNECT " + j.value("name", "Player");
        line += " level:"  + std::to_string(j.value("level",  1));
        line += " gender:" + std::to_string(j.value("gender", 0));
        line += " job:"    + j.value("job", "warrior");
    } else if (cmd == "move") {
        std::ostringstream ss;
        ss << "MOVE " << j.value("x", 0.f) << " " << j.value("y", 0.f);
        line = ss.str();
    } else if (cmd == "attack") {
        line = "ATTACK " + std::to_string(j.value("monsterId", 0));
        if (j.count("damage")) line += " damage:" + std::to_string(j.value("damage", 10));
    } else if (cmd == "struck") {
        line = "STRUCK " + std::to_string(j.value("monsterId", 0));
    } else if (cmd == "kill") {
        line = "KILL " + std::to_string(j.value("monsterId", 0));
    } else if (cmd == "buff") {
        std::string a = j.value("action", "add");
        std::transform(a.begin(), a.end(), a.begin(), ::toupper);
        line = "BUFF " + a + " " + std::to_string(j.value("buffId", 0));
    } else if (cmd == "item") {
        std::string action = j.value("action", "use");
        std::transform(action.begin(), action.end(), action.begin(), ::toupper);
        line = "ITEM " + action + " " + std::to_string(j.value("itemId", 0));
    } else if (cmd == "pickup") {
        line = "PICKUP " + std::to_string(j.value("groundId", 0));
    } else if (cmd == "shop") {
        std::string action = j.value("action", "list");
        std::transform(action.begin(), action.end(), action.begin(), ::toupper);
        line = "SHOP " + action;
        if (j.count("itemId")) line += " " + std::to_string(j.value("itemId", 0));
        if (j.count("qty"))    line += " " + std::to_string(j.value("qty", 1));
    } else if (cmd == "quest") {
        std::string a = j.value("action", "accept");
        std::transform(a.begin(), a.end(), a.begin(), ::toupper);
        line = "QUEST " + a + " " + std::to_string(j.value("questId", 0));
    } else if (cmd == "status") {
        line = "STATUS";
    } else if (cmd == "look") {
        line = "LOOK";
    } else if (cmd == "reload") {
        line = "RELOAD " + j.value("module", "");
    } else if (cmd == "quit") {
        return;
    } else {
        wsSend(json{{"type","error"},{"msg","Unknown cmd: "+cmd}}.dump());
        wsSend(gs_.getStateJson(playerId_));
        return;
    }

    std::string resp = gs_.processCommand(playerId_, line);
    bool isErr = resp.rfind("ERR:", 0) == 0;
    // JSON 응답은 그대로 전송 (상점 목록 등)
    if (!resp.empty() && resp[0] == '{') {
        wsSend(resp);
    } else {
        wsSend(json{{"type", isErr ? "error" : "ok"}, {"msg", resp}}.dump());
    }
    wsSend(gs_.getStateJson(playerId_));

    // Broadcast state change to all other connected clients
    // (skip read-only commands that don't change game state)
    if (!isErr && cmd != "status" && cmd != "look" && cmd != "reload") {
        gs_.broadcastToOthers(playerId_);
    }
}

// ──── Thread-safe push ─────────────────────────────────────────────

void WsSession::pushJson(const std::string& jsonStr) {
    // Post to this session's executor so wsSend is called on the correct thread
    asio::post(socket_.get_executor(),
        [self = shared_from_this(), jsonStr]() {
            self->wsSend(jsonStr);
        });
}

// ──── WebSocket Write ──────────────────────────────────────────────

void WsSession::wsSend(const std::string& text) {
    std::string frame;
    frame.push_back('\x81');  // FIN=1, opcode=1 (text)
    size_t len = text.size();
    if (len <= 125) {
        frame.push_back((char)len);
    } else if (len <= 65535) {
        frame.push_back('\x7E');
        frame.push_back((len >> 8) & 0xFF);
        frame.push_back(len & 0xFF);
    } else {
        frame.push_back('\x7F');
        for (int i = 7; i >= 0; --i) frame.push_back((len >> (i*8)) & 0xFF);
    }
    frame += text;
    wsSendRaw(std::move(frame));
}

void WsSession::wsSendRaw(std::string data) {
    writeQueue_.push_back(std::move(data));
    if (!writing_) flushWrite();
}

void WsSession::flushWrite() {
    if (writeQueue_.empty()) { writing_ = false; return; }
    writing_ = true;
    auto self = shared_from_this();
    asio::async_write(socket_, asio::buffer(writeQueue_.front()),
        [this, self](std::error_code ec, size_t) {
            writeQueue_.pop_front();
            if (!ec) flushWrite();
            else     writing_ = false;
        });
}

// ================================================================
// WsServer
// ================================================================

WsServer::WsServer(GameServer& gs, int port)
    : gs_(gs), port_(port), ioc_(), acceptor_(ioc_)
{}

WsServer::~WsServer() { stop(); }

void WsServer::start() {
    asio::ip::tcp::endpoint ep(asio::ip::tcp::v4(), (unsigned short)port_);
    acceptor_.open(ep.protocol());
    acceptor_.set_option(asio::ip::tcp::acceptor::reuse_address(true));
    acceptor_.bind(ep);
    acceptor_.listen();
    std::cout << "[WsServer] HTTP+WS on http://localhost:" << port_ << "\n";
    doAccept();
    thread_ = std::thread([this] { ioc_.run(); });
}

void WsServer::stop() {
    asio::post(ioc_, [this] { acceptor_.close(); });
    ioc_.stop();
    if (thread_.joinable()) thread_.join();
}

void WsServer::doAccept() {
    acceptor_.async_accept(
        [this](std::error_code ec, asio::ip::tcp::socket socket) {
            if (ec) {
                if (ec != asio::error::operation_aborted)
                    std::cerr << "[WsServer] accept: " << ec.message() << "\n";
                return;
            }
            std::make_shared<WsSession>(std::move(socket), gs_, *this)->start();
            doAccept();
        });
}

// ──── Session registry ─────────────────────────────────────────────

void WsServer::addSession(int playerId, std::shared_ptr<WsSession> session) {
    std::lock_guard<std::mutex> lock(sessionsMutex_);
    sessions_[playerId] = session;
    std::cout << "[WsServer] Session " << playerId << " registered ("
              << sessions_.size() << " total)\n";
}

void WsServer::removeSession(int playerId) {
    std::lock_guard<std::mutex> lock(sessionsMutex_);
    sessions_.erase(playerId);
    std::cout << "[WsServer] Session " << playerId << " removed ("
              << sessions_.size() << " remaining)\n";
}

void WsServer::broadcastExcept(int excludeId, std::function<std::string(int)> stateGetter) {
    // Snapshot live sessions under lock, then release before computing state JSON
    std::vector<std::pair<int, std::shared_ptr<WsSession>>> snapshot;
    {
        std::lock_guard<std::mutex> lock(sessionsMutex_);
        for (auto& [id, weak] : sessions_) {
            if (id == excludeId) continue;
            if (auto sess = weak.lock()) snapshot.emplace_back(id, sess);
        }
    }

    // Compute per-player state and push (outside the sessions lock)
    for (auto& [id, sess] : snapshot) {
        sess->pushJson(stateGetter(id));
    }
}
