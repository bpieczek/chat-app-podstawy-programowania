
#pragma once
#include <map>
#include <memory>
#include <mutex>
#include <set>
#include <string>
#include <thread>
#include "Message.h"
#include "Logger.h"
#include <atomic>
#include <vector>

class ClientHandler;

class ChatServer {
  public:
    void testBroadcast();
    ChatServer(int port);
    std::vector<std::string> getOnlineUsers() const;
    ~ChatServer();
    bool isRunning() const {
        return running_;
    }
    void scheduleClientRemoval(ClientHandler* client);
    void processRawMessage(ClientHandler* sender, const std::string& raw_msg);
    void start();
    void stop();
    void addClient(std::shared_ptr<ClientHandler> client);
    void removeClient(ClientHandler* client);
    void processMessage(ClientHandler* sender, const Message& msg);
    void clientDisconnected(ClientHandler* client);
    void broadcast(const std::string& message, ClientHandler* exclude = nullptr);
    void privateMessage(const std::string& message,
                        const std::string& receiver);
    void stopClients();
    void processScheduledRemovals();
  private:
    void run();
    int getNextAvailableUserNumber() const;
    std::mutex removal_mutex_;
    std::vector<ClientHandler*> clients_to_remove_;
    int port_;
    int server_socket_;
    std::atomic<bool> running_;
    std::unique_ptr<std::thread> main_thread_;
    std::set<std::shared_ptr<ClientHandler>> clients_;
    std::map<std::string, std::shared_ptr<ClientHandler>> nicknames_;
    mutable std::mutex clients_mutex_;
    Logger<std::string> logger_;
};
