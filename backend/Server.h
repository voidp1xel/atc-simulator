/**
 * Server.h - WebSocket + HTTP Server
 *
 * A zero-dependency networking layer built on POSIX sockets.
 * - Serves frontend static files over HTTP (index.html, .css, .js).
 * - Upgrades qualifying requests to WebSocket (RFC 6455).
 * - Broadcasts JSON telemetry to all connected WebSocket clients at 10 Hz.
 * - Receives and parses ATC commands from clients.
 *
 * Includes inline SHA-1 and Base64 implementations to avoid any
 * external cryptographic library dependency.
 */

#pragma once
#include <string>
#include <vector>
#include <mutex>
#include <atomic>
#include <thread>
#include "SimulationEngine.h"

class Server {
public:
    Server(SimulationEngine& engine, int port = 8080);
    ~Server();

    /** Start accept loop and broadcast loop on their own threads. */
    void start();

    /** Shut down all threads and close all connections. */
    void stop();

private:
    SimulationEngine& engine;
    int port;
    int serverFd = -1;

    /* ── Connected WebSocket client fds ──────────────────────────── */
    std::vector<int> clients;
    std::mutex clientsMutex;

    std::atomic<bool> running{false};

    std::thread acceptThread;
    std::thread broadcastThread;

    /* ── Network loops ───────────────────────────────────────────── */
    void acceptLoop();
    void broadcastLoop();
    void handleClient(int fd);

    /* ── WebSocket protocol helpers ──────────────────────────────── */
    bool performHandshake(int fd, const std::string& httpRequest);
    std::string readFrame(int fd);
    void sendFrame(int fd, const std::string& payload);
    void removeClient(int fd);

    /* ── HTTP static file serving ────────────────────────────────── */
    void serveStaticFile(int fd, const std::string& httpRequest);
    std::string getMimeType(const std::string& path);

    /* ── Inline crypto (no external deps) ────────────────────────── */
    static std::string sha1Hash(const std::string& input);
    static std::string base64Encode(const unsigned char* data, size_t len);

    /* ── Command parsing ─────────────────────────────────────────── */
    void parseAndEnqueueCommand(const std::string& json);
};
