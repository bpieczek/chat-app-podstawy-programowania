#include "ChatServer.h"
#include <csignal>
#include <iostream>
#include <memory>
#include <thread>
#include <chrono>
#include <atomic>

std::unique_ptr<ChatServer> server;
volatile std::sig_atomic_t gSignalStatus;

void signalHandler(int signal) {
    gSignalStatus = signal;
    if(server) {
        server->stop();
    }
}

int main() {
    const int PORT = 55555;
    server = std::make_unique<ChatServer>(PORT);

    std::signal(SIGINT, signalHandler);
    std::signal(SIGTERM, signalHandler);

    try {
        server->start();
        std::cout << "Server running. Press Ctrl+C to stop.\n";

        while(gSignalStatus == 0) {
            std::this_thread::sleep_for(std::chrono::seconds(1));
        }

        std::cout << "\nReceived signal " << gSignalStatus
                  << ", shutting down gracefully...\n";
    } catch(const std::exception& e) {
        std::cerr << "Server error: " << e.what() << std::endl;
        return 1;
    }

    return 0;
}