#pragma once
#include <memory>
#include <string>
#include <thread>
#include <sys/socket.h>
#include <unistd.h>
#include "Message.h"
#include <iostream>
#include <mutex>

class ChatServer;

class ClientHandler {
  public:
    void debugInfo() const {
        std::cout << "ClientHandler [Socket: " << client_socket_
                  << ", Nick: " << nickname_
                  << ", Active: " << active_
                  << ", Thread: " << (thread_ ? "yes" : "no")
                  << "]\n";
    }
    void clearLine();
    ClientHandler(int socket, ChatServer* server, const std::string& defaultNickname);
    ~ClientHandler();
    void sendPrompt();
    void start();
    void sendMessage(const std::string& msg);
    std::string getNickname() const;
    void setNickname(const std::string& nickname);
    int getSocket() const {
        return client_socket_;
    }

    void stopClient() {
        if(active_) {
            active_ = false;
            if(client_socket_ != -1) {
                shutdown(client_socket_, SHUT_RDWR);
                close(client_socket_);
                client_socket_ = -1;
            }
        }
    }

  private:
    void run();
    void handleMessage(const std::string& msg);
    std::mutex socket_mutex_;
    bool prompt_pending_ = true;
    int client_socket_;
    std::string nickname_;
    ChatServer* server_;
    std::unique_ptr<std::thread> thread_;
    bool active_;
};