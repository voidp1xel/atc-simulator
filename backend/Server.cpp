/**
 * Server.cpp - WebSocket + HTTP Server Implementation
 *
 * Architecture:
 *   acceptThread   – blocks on accept(), dispatches each connection either
 *                    as an HTTP file request or a WebSocket upgrade.
 *   broadcastThread – every 100 ms (10 Hz), fetches telemetry JSON from the
 *                     SimulationEngine and sends it to all WebSocket clients.
 *   per-client thread – reads WebSocket frames (ATC commands) from each
 *                       connected controller and pushes them into the
 *                       engine's command queue.
 *
 * WebSocket handshake follows RFC 6455 §4.2.2: the server computes
 * SHA-1(client key + magic GUID) and returns it Base64-encoded.
 */

#include "Server.h"

#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <signal.h>
#include <fcntl.h>

#include <cstring>
#include <iostream>
#include <sstream>
#include <fstream>
#include <algorithm>
#include <chrono>
#include <thread>
#include <functional>
#include <stdexcept>

/* ── WebSocket magic GUID (RFC 6455 §1.3) ───────────────────────────── */
static const std::string WS_MAGIC = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";

/* ══════════════════════════════════════════════════════════════════════
 *  SHA-1  (FIPS 180-4, inline – ~60 lines, no dependencies)
 * ══════════════════════════════════════════════════════════════════════ */

static inline uint32_t rotl32(uint32_t v, int n) {
    return (v << n) | (v >> (32 - n));
}

std::string Server::sha1Hash(const std::string& input) {
    uint32_t h0 = 0x67452301, h1 = 0xEFCDAB89,
             h2 = 0x98BADCFE, h3 = 0x10325476, h4 = 0xC3D2E1F0;

    // Pre-processing: pad to 512-bit boundary
    std::string msg = input;
    uint64_t bits = msg.size() * 8;
    msg += static_cast<char>(0x80);
    while (msg.size() % 64 != 56) msg += static_cast<char>(0x00);
    for (int i = 7; i >= 0; --i)
        msg += static_cast<char>((bits >> (i * 8)) & 0xFF);

    // Process 512-bit blocks
    for (size_t off = 0; off < msg.size(); off += 64) {
        uint32_t w[80];
        for (int i = 0; i < 16; ++i)
            w[i] = (uint32_t)(unsigned char)msg[off+i*4]   << 24 |
                   (uint32_t)(unsigned char)msg[off+i*4+1] << 16 |
                   (uint32_t)(unsigned char)msg[off+i*4+2] <<  8 |
                   (uint32_t)(unsigned char)msg[off+i*4+3];
        for (int i = 16; i < 80; ++i)
            w[i] = rotl32(w[i-3] ^ w[i-8] ^ w[i-14] ^ w[i-16], 1);

        uint32_t a=h0, b=h1, c=h2, d=h3, e=h4;
        for (int i = 0; i < 80; ++i) {
            uint32_t f, k;
            if      (i < 20) { f = (b&c) | (~b&d);          k = 0x5A827999; }
            else if (i < 40) { f = b^c^d;                   k = 0x6ED9EBA1; }
            else if (i < 60) { f = (b&c) | (b&d) | (c&d);  k = 0x8F1BBCDC; }
            else              { f = b^c^d;                   k = 0xCA62C1D6; }
            uint32_t t = rotl32(a,5) + f + e + k + w[i];
            e=d; d=c; c=rotl32(b,30); b=a; a=t;
        }
        h0+=a; h1+=b; h2+=c; h3+=d; h4+=e;
    }

    // Produce 20-byte digest
    unsigned char digest[20];
    for (int i = 0; i < 4; ++i) {
        digest[i]    = (h0 >> (24 - i*8)) & 0xFF;
        digest[i+4]  = (h1 >> (24 - i*8)) & 0xFF;
        digest[i+8]  = (h2 >> (24 - i*8)) & 0xFF;
        digest[i+12] = (h3 >> (24 - i*8)) & 0xFF;
        digest[i+16] = (h4 >> (24 - i*8)) & 0xFF;
    }
    return std::string(reinterpret_cast<char*>(digest), 20);
}

/* ══════════════════════════════════════════════════════════════════════
 *  Base64 encoder
 * ══════════════════════════════════════════════════════════════════════ */

std::string Server::base64Encode(const unsigned char* data, size_t len) {
    static const char T[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string r;
    for (size_t i = 0; i < len; i += 3) {
        uint32_t n = (uint32_t)data[i] << 16;
        if (i+1 < len) n |= (uint32_t)data[i+1] << 8;
        if (i+2 < len) n |= data[i+2];
        r += T[(n>>18)&0x3F];
        r += T[(n>>12)&0x3F];
        r += (i+1<len) ? T[(n>>6)&0x3F] : '=';
        r += (i+2<len) ? T[n&0x3F]      : '=';
    }
    return r;
}

/* ══════════════════════════════════════════════════════════════════════
 *  Constructor / Destructor
 * ══════════════════════════════════════════════════════════════════════ */

Server::Server(SimulationEngine& eng, int p) : engine(eng), port(p) {}

Server::~Server() { stop(); }

/* ══════════════════════════════════════════════════════════════════════
 *  Lifecycle
 * ══════════════════════════════════════════════════════════════════════ */

void Server::start() {
    // Ignore SIGPIPE so writes to closed sockets don't crash us
    signal(SIGPIPE, SIG_IGN);

    serverFd = socket(AF_INET, SOCK_STREAM, 0);
    if (serverFd < 0) { perror("socket"); return; }

    int opt = 1;
    setsockopt(serverFd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    sockaddr_in addr{};
    addr.sin_family      = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port        = htons(port);

    if (bind(serverFd, (sockaddr*)&addr, sizeof(addr)) < 0) {
        perror("bind"); return;
    }
    if (listen(serverFd, 16) < 0) {
        perror("listen"); return;
    }

    running = true;
    std::cout << "[Server] Listening on http://localhost:" << port << "\n";

    acceptThread    = std::thread(&Server::acceptLoop, this);
    broadcastThread = std::thread(&Server::broadcastLoop, this);
}

void Server::stop() {
    running = false;
    if (serverFd >= 0) { close(serverFd); serverFd = -1; }
    if (acceptThread.joinable())    acceptThread.join();
    if (broadcastThread.joinable()) broadcastThread.join();
}

/* ══════════════════════════════════════════════════════════════════════
 *  Accept loop – classify each connection as HTTP or WebSocket
 * ══════════════════════════════════════════════════════════════════════ */

void Server::acceptLoop() {
    while (running) {
        sockaddr_in ca{};
        socklen_t cl = sizeof(ca);
        int fd = accept(serverFd, (sockaddr*)&ca, &cl);
        if (fd < 0) { if (running) perror("accept"); continue; }

        // Read the initial HTTP request
        char buf[4096];
        ssize_t n = recv(fd, buf, sizeof(buf)-1, 0);
        if (n <= 0) { close(fd); continue; }
        buf[n] = '\0';
        std::string req(buf, n);

        // Does the client want a WebSocket upgrade?
        if (req.find("Upgrade: websocket") != std::string::npos ||
            req.find("Upgrade: WebSocket") != std::string::npos)
        {
            if (performHandshake(fd, req)) {
                {
                    std::lock_guard<std::mutex> lk(clientsMutex);
                    clients.push_back(fd);
                }
                std::cout << "[Server] WebSocket client connected (fd="
                          << fd << ")\n";
                // Spawn a reader thread for this client
                std::thread(&Server::handleClient, this, fd).detach();
            } else {
                close(fd);
            }
        } else {
            // Plain HTTP – serve a static file and close
            serveStaticFile(fd, req);
            close(fd);
        }
    }
}

/* ══════════════════════════════════════════════════════════════════════
 *  Broadcast loop – push telemetry at 10 Hz
 * ══════════════════════════════════════════════════════════════════════ */

void Server::broadcastLoop() {
    while (running) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));

        std::string json = engine.getTelemetryJSON();

        std::lock_guard<std::mutex> lk(clientsMutex);
        auto it = clients.begin();
        while (it != clients.end()) {
            try {
                sendFrame(*it, json);
                ++it;
            } catch (...) {
                std::cout << "[Server] Client fd=" << *it << " dropped\n";
                close(*it);
                it = clients.erase(it);
            }
        }
    }
}

/* ══════════════════════════════════════════════════════════════════════
 *  Per-client reader thread
 * ══════════════════════════════════════════════════════════════════════ */

void Server::handleClient(int fd) {
    while (running) {
        std::string msg = readFrame(fd);
        if (msg.empty()) break;              // connection closed or error
        parseAndEnqueueCommand(msg);
    }
    removeClient(fd);
}

/* ══════════════════════════════════════════════════════════════════════
 *  WebSocket handshake (RFC 6455 §4.2.2)
 * ══════════════════════════════════════════════════════════════════════ */

bool Server::performHandshake(int fd, const std::string& req) {
    // Extract Sec-WebSocket-Key header value
    auto pos = req.find("Sec-WebSocket-Key: ");
    if (pos == std::string::npos) return false;
    pos += 19;
    auto end = req.find("\r\n", pos);
    std::string key = req.substr(pos, end - pos);

    // Compute accept value: Base64(SHA-1(key + magic))
    std::string sha = sha1Hash(key + WS_MAGIC);
    std::string accept = base64Encode(
        reinterpret_cast<const unsigned char*>(sha.data()), sha.size());

    // Send the 101 Switching Protocols response
    std::string resp =
        "HTTP/1.1 101 Switching Protocols\r\n"
        "Upgrade: websocket\r\n"
        "Connection: Upgrade\r\n"
        "Sec-WebSocket-Accept: " + accept + "\r\n\r\n";

    return send(fd, resp.c_str(), resp.size(), 0) > 0;
}

/* ══════════════════════════════════════════════════════════════════════
 *  WebSocket frame I/O (RFC 6455 §5)
 * ══════════════════════════════════════════════════════════════════════ */

std::string Server::readFrame(int fd) {
    unsigned char header[2];
    if (recv(fd, header, 2, MSG_WAITALL) != 2) return "";

    int opcode = header[0] & 0x0F;
    if (opcode == 0x8) return "";             // close frame

    bool masked = header[1] & 0x80;
    uint64_t payloadLen = header[1] & 0x7F;

    if (payloadLen == 126) {
        unsigned char ex[2];
        if (recv(fd, ex, 2, MSG_WAITALL) != 2) return "";
        payloadLen = (uint64_t)ex[0] << 8 | ex[1];
    } else if (payloadLen == 127) {
        unsigned char ex[8];
        if (recv(fd, ex, 8, MSG_WAITALL) != 8) return "";
        payloadLen = 0;
        for (int i = 0; i < 8; ++i)
            payloadLen = (payloadLen << 8) | ex[i];
    }

    unsigned char mask[4] = {};
    if (masked) {
        if (recv(fd, mask, 4, MSG_WAITALL) != 4) return "";
    }

    std::string payload(payloadLen, '\0');
    if (payloadLen > 0) {
        ssize_t total = 0;
        while (total < (ssize_t)payloadLen) {
            ssize_t r = recv(fd, &payload[total], payloadLen - total, 0);
            if (r <= 0) return "";
            total += r;
        }
        if (masked) {
            for (uint64_t i = 0; i < payloadLen; ++i)
                payload[i] ^= mask[i % 4];
        }
    }
    return payload;
}

void Server::sendFrame(int fd, const std::string& payload) {
    std::vector<unsigned char> frame;
    frame.push_back(0x81);               // FIN + text opcode

    if (payload.size() < 126) {
        frame.push_back(static_cast<unsigned char>(payload.size()));
    } else if (payload.size() <= 65535) {
        frame.push_back(126);
        frame.push_back((payload.size() >> 8) & 0xFF);
        frame.push_back( payload.size()       & 0xFF);
    } else {
        frame.push_back(127);
        for (int i = 7; i >= 0; --i)
            frame.push_back((payload.size() >> (i*8)) & 0xFF);
    }

    frame.insert(frame.end(), payload.begin(), payload.end());

    ssize_t sent = 0;
    while (sent < (ssize_t)frame.size()) {
        ssize_t s = ::send(fd, frame.data()+sent, frame.size()-sent, 0);
        if (s <= 0) throw std::runtime_error("send failed");
        sent += s;
    }
}

void Server::removeClient(int fd) {
    close(fd);
    std::lock_guard<std::mutex> lk(clientsMutex);
    clients.erase(std::remove(clients.begin(), clients.end(), fd), clients.end());
    std::cout << "[Server] WebSocket client disconnected (fd=" << fd << ")\n";
}

/* ══════════════════════════════════════════════════════════════════════
 *  HTTP static file serving
 * ══════════════════════════════════════════════════════════════════════ */

std::string Server::getMimeType(const std::string& path) {
    if (path.size() >= 5 && path.substr(path.size()-5) == ".html") return "text/html";
    if (path.size() >= 4 && path.substr(path.size()-4) == ".css")  return "text/css";
    if (path.size() >= 3 && path.substr(path.size()-3) == ".js")   return "application/javascript";
    if (path.size() >= 4 && path.substr(path.size()-4) == ".png")  return "image/png";
    if (path.size() >= 4 && path.substr(path.size()-4) == ".ico")  return "image/x-icon";
    return "application/octet-stream";
}

void Server::serveStaticFile(int fd, const std::string& req) {
    // Parse "GET /path HTTP/1.1"
    std::string path = "/index.html";
    auto g = req.find("GET ");
    if (g != std::string::npos) {
        auto e = req.find(" HTTP", g + 4);
        if (e != std::string::npos)
            path = req.substr(g + 4, e - g - 4);
    }
    if (path == "/") path = "/index.html";

    // Map URL to local file (relative to working directory)
    std::string filePath = "frontend" + path;

    std::ifstream file(filePath, std::ios::binary);
    if (!file.is_open()) {
        std::string r = "HTTP/1.1 404 Not Found\r\n"
                        "Content-Length: 9\r\n\r\nNot Found";
        send(fd, r.c_str(), r.size(), 0);
        return;
    }

    std::ostringstream body;
    body << file.rdbuf();
    std::string content = body.str();

    std::string mime = getMimeType(path);
    std::ostringstream resp;
    resp << "HTTP/1.1 200 OK\r\n"
         << "Content-Type: " << mime << "\r\n"
         << "Content-Length: " << content.size() << "\r\n"
         << "Cache-Control: no-cache\r\n"
         << "Connection: close\r\n\r\n"
         << content;

    std::string r = resp.str();
    send(fd, r.c_str(), r.size(), 0);
}

/* ══════════════════════════════════════════════════════════════════════
 *  Command JSON parsing (minimal, no library)
 * ══════════════════════════════════════════════════════════════════════ */

static std::string extractJSONString(const std::string& json,
                                     const std::string& key) {
    std::string search = "\"" + key + "\":\"";
    auto pos = json.find(search);
    if (pos == std::string::npos) return "";
    pos += search.size();
    auto end = json.find('"', pos);
    return (end == std::string::npos) ? "" : json.substr(pos, end - pos);
}

static double extractJSONNumber(const std::string& json,
                                const std::string& key) {
    std::string search = "\"" + key + "\":";
    auto pos = json.find(search);
    if (pos == std::string::npos) return 0;
    pos += search.size();
    // Skip whitespace
    while (pos < json.size() && (json[pos]==' '||json[pos]=='\t')) ++pos;
    try { return std::stod(json.substr(pos)); }
    catch (...) { return 0; }
}

void Server::parseAndEnqueueCommand(const std::string& json) {
    std::string type = extractJSONString(json, "type");
    if (type != "command") return;

    Command cmd;
    cmd.callsign = extractJSONString(json, "callsign");
    cmd.action   = extractJSONString(json, "action");
    cmd.value    = extractJSONNumber(json, "value");

    if (!cmd.callsign.empty() && !cmd.action.empty()) {
        engine.enqueueCommand(cmd);
    }
}
