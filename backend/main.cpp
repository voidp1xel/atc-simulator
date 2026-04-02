/**
 * main.cpp - ATC Sector Simulator Entry Point
 *
 * Boots the simulation engine and HTTP/WebSocket server.
 * The simulation physics run on a dedicated thread (20 Hz).
 * The server runs on the main thread, accepting connections
 * and broadcasting telemetry at 10 Hz.
 *
 * Usage:  ./atc-simulator [port]
 *         Default port is 8080.
 */

#include "SimulationEngine.h"
#include "Server.h"
#include <iostream>
#include <csignal>
#include <atomic>

static std::atomic<bool> g_running{true};

static void signalHandler(int) {
    g_running = false;
}

int main(int argc, char* argv[]) {
    int port = 8080;
    if (argc > 1) {
        port = std::atoi(argv[1]);
        if (port <= 0 || port > 65535) {
            std::cerr << "Invalid port: " << argv[1] << "\n";
            return 1;
        }
    }

    std::signal(SIGINT,  signalHandler);
    std::signal(SIGTERM, signalHandler);

    std::cout << "╔══════════════════════════════════════════════╗\n";
    std::cout << "║   ATC Sector Simulator                      ║\n";
    std::cout << "║   Real-Time Air Traffic Control Engine       ║\n";
    std::cout << "╚══════════════════════════════════════════════╝\n\n";

    // 1. Create the simulation engine
    SimulationEngine engine;
    engine.start();

    // 2. Create and start the network server
    Server server(engine, port);
    server.start();

    std::cout << "\n[Main] Open http://localhost:" << port
              << " in your browser.\n"
              << "[Main] Press Ctrl+C to shut down.\n\n";

    // 3. Keep main thread alive until interrupted
    while (g_running) {
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }

    std::cout << "\n[Main] Shutting down...\n";
    server.stop();
    engine.stop();

    return 0;
}
