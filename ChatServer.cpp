#include "ChatServer.h"
#include "ClientHandler.h"
#include <arpa/inet.h>
#include <iostream>
#include <netinet/in.h>
#include <stdexcept>
#include <sys/socket.h>
#include <unistd.h>
#include <netinet/tcp.h>
#include <cstring>
#include <algorithm>
#include <iomanip>
#include <ctime>
#include <chrono>
#include <sstream>
#include <fcntl.h>

ChatServer::ChatServer(int port)
    : port_(port), server_socket_(-1), running_(false), logger_("log.txt") {}

ChatServer::~ChatServer() {
    stop();
}

std::string getTimestamp() {
    auto now = std::chrono::system_clock::now();
    auto in_time_t = std::chrono::system_clock::to_time_t(now);
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                  now.time_since_epoch()) % 1000;

    std::tm bt = *std::localtime(&in_time_t);

    std::ostringstream oss;
    oss << std::put_time(&bt, "%Y-%m-%d %H:%M:%S");
    oss << '.' << std::setfill('0') << std::setw(3) << ms.count();
    return oss.str();
}

void ChatServer::start() {
    server_socket_ = socket(AF_INET, SOCK_STREAM, 0);
    if(server_socket_ < 0) {
        throw std::runtime_error("Socket creation failed");
    }

    int flags = fcntl(server_socket_, F_GETFL, 0);
    if(flags < 0) {
        close(server_socket_);
        throw std::runtime_error("fcntl(F_GETFL) failed");
    }
    if(fcntl(server_socket_, F_SETFL, flags | O_NONBLOCK) < 0) {
        close(server_socket_);
        throw std::runtime_error("fcntl(F_SETFL) failed");
    }

    int opt = 1;
    if(setsockopt(server_socket_, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
        close(server_socket_);
        throw std::runtime_error("setsockopt failed");
    }

    sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(port_);

    if(bind(server_socket_, (sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        close(server_socket_);
        throw std::runtime_error("Bind failed");
    }

    if(listen(server_socket_, 5) < 0) {
        close(server_socket_);
        throw std::runtime_error("Listen failed");
    }

    running_ = true;
    main_thread_ = std::make_unique<std::thread>(&ChatServer::run, this);
    logger_.log("[" + getTimestamp() + "] Server started on port " + std::to_string(port_));
}

void ChatServer::stopClients() {
    std::vector<std::shared_ptr<ClientHandler>> clients_copy;
    {
        std::lock_guard<std::mutex> lock(clients_mutex_);
        clients_copy = {clients_.begin(), clients_.end()};
    }

    for(auto& client : clients_copy) {
        client->sendMessage("\033[1;36m[System] Server is shutting down. Disconnecting...\033[0m");
        client->stopClient();
    }

    {
        std::lock_guard<std::mutex> lock(clients_mutex_);
        clients_.clear();
        nicknames_.clear();
    }
}

void ChatServer::removeClient(ClientHandler* client) {
    std::lock_guard<std::mutex> lock(clients_mutex_);

    auto nick_it = nicknames_.find(client->getNickname());
    if(nick_it != nicknames_.end() && nick_it->second.get() == client) {
        nicknames_.erase(nick_it);
    }

    for(auto it = clients_.begin(); it != clients_.end();) {
        if(it->get() == client) {
            it = clients_.erase(it);
            break;
        } else {
            ++it;
        }
    }
}

void ChatServer::stop() {
    if(!running_) return;

    running_ = false;


    if(server_socket_ != -1) {
        close(server_socket_);
        server_socket_ = -1;
    }

    stopClients();

    {
        std::lock_guard<std::mutex> lock(clients_mutex_);
        clients_.clear();
        nicknames_.clear();
    }

    if(main_thread_ && main_thread_->joinable()) {
        main_thread_->join();
    }
}

void ChatServer::run() {
    std::cout << "[" << getTimestamp() << "] Server main thread started" << std::endl;
    logger_.log("[" + getTimestamp() + "] Server main thread started");

    fd_set read_fds;
    struct timeval timeout;

    while(running_) {
        processScheduledRemovals();

        FD_ZERO(&read_fds);
        FD_SET(server_socket_, &read_fds);

        timeout.tv_sec = 1;
        timeout.tv_usec = 0;

        int ready = select(server_socket_ + 1, &read_fds, nullptr, nullptr, &timeout);

        if(!running_) break;

        if(ready < 0) {
            if(errno == EINTR) {
                continue;
            }
            logger_.log("[" + getTimestamp() + "] Select error: " + std::string(strerror(errno)));
            std::cerr << "[" << getTimestamp() << "] [ERROR] select() failed: "
                      << strerror(errno) << std::endl;
            continue;
        }

        if(ready == 0) {
            continue;
        }

        if(FD_ISSET(server_socket_, &read_fds)) {
            sockaddr_in client_addr;
            socklen_t client_len = sizeof(client_addr);
            int client_socket = accept(server_socket_, (sockaddr*)&client_addr, &client_len);

            if(!running_) {
                if(client_socket != -1) close(client_socket);
                break;
            }

            if(client_socket < 0) {
                if(running_) {
                    logger_.log("[" + getTimestamp() + "] Accept failed: " + std::string(strerror(errno)));
                    std::cerr << "[" << getTimestamp() << "] [ERROR] accept() failed: "
                              << strerror(errno) << std::endl;
                }
                continue;
            }

            if(client_socket >= 0) {
                char client_ip[INET_ADDRSTRLEN];
                inet_ntop(AF_INET, &client_addr.sin_addr, client_ip, INET_ADDRSTRLEN);
                int client_port = ntohs(client_addr.sin_port);

                std::cout << "[" << getTimestamp() << "] New client connected: "
                          << client_ip << ":" << client_port
                          << " (socket: " << client_socket << ")" << std::endl;
            }

            int userNumber = getNextAvailableUserNumber();
            std::string defaultNick = "User" + std::to_string(userNumber);
            auto client = std::make_shared<ClientHandler>(client_socket, this, defaultNick);

            bool clientExists = false;
            {
                std::lock_guard<std::mutex> lock(clients_mutex_);
                for(const auto& existing : clients_) {
                    if(existing->getSocket() == client_socket) {
                        clientExists = true;
                        break;
                    }
                }

                if(!clientExists) {
                    clients_.insert(client);
                    nicknames_[client->getNickname()] = client;
                }
            }

            if(clientExists) {
                std::cerr << "[" << getTimestamp() << "] Client already exists: "
                          << client_socket << std::endl;
                close(client_socket);
            } else {
                client->start();
                const std::string nickname = client->getNickname();
                const int totalWidth = 40;
                const int prefixLen = 18;
                const int suffixLen = 0;
                const int spacesNeeded = totalWidth - prefixLen - nickname.length() - suffixLen;

                std::string nickLine = "| Your nickname: " + nickname;
                if(spacesNeeded > 0) {
                    nickLine += std::string(spacesNeeded, ' ');
                }
                nickLine += "|";

                client->sendMessage("----------------------------------------");
                client->sendMessage("| Welcome to the chat server!          |");
                client->sendMessage(nickLine);
                client->sendMessage("| Use /nick <new_nick> to change nick  |");
                client->sendMessage("| Use /pm <nick> <message> for PM      |");
                client->sendMessage("| Use /users to list online users      |");
                client->sendMessage("| Use /leave to exit the chat          |");
                client->sendMessage("----------------------------------------");

                std::string sys_msg = "\033[1;36m[System] " + client->getNickname() + " joined\033[0m";
                broadcast(sys_msg, nullptr);

                char client_ip[INET_ADDRSTRLEN];
                inet_ntop(AF_INET, &client_addr.sin_addr, client_ip, INET_ADDRSTRLEN);
                logger_.log("[" + getTimestamp() + "] Client connected: " + std::string(client_ip) + ":" + std::to_string(ntohs(client_addr.sin_port)));
            }
        }
    }

    stopClients();

    processScheduledRemovals();

    logger_.log("[" + getTimestamp() + "] Server main thread stopped");
    std::cout << "[" << getTimestamp() << "] Server main thread stopped" << std::endl;
}


void ChatServer::addClient(std::shared_ptr<ClientHandler> client) {
    std::lock_guard<std::mutex> lock(clients_mutex_);
    clients_.insert(client);
    nicknames_[client->getNickname()] = client;
}

void ChatServer::clientDisconnected(ClientHandler* client) {
    removeClient(client);

    if(running_) {
        std::string sys_msg = "\033[1;36m[System] " + client->getNickname() + " left the chat\033[0m";
        broadcast(sys_msg, nullptr);
        logger_.log("[" + getTimestamp() + "] Client disconnected: " + client->getNickname());
    }
}

void ChatServer::processMessage(ClientHandler* sender, const Message& msg) {
    switch(msg.getType()) {
    case MessageType::Broadcast: {
        std::string formatted = "[" + sender->getNickname() + "] " + msg.getContent();
        std::cout << "[" << getTimestamp() << "] [BROADCAST] Sending: " << formatted << std::endl;
        broadcast(formatted, sender);
        logger_.log("[" + getTimestamp() + "] BROADCAST: " + formatted);
        break;
    }
    case MessageType::Private: {
        std::string receiver = msg.getReceiver();
        auto it = nicknames_.find(receiver);

        if(it != nicknames_.end()) {
            std::string to_receiver = "\033[1;35m[PM from " + sender->getNickname() + "]\033[0m " + msg.getContent();
            std::string to_sender = "\033[1;35m[PM to " + receiver + "]\033[0m " + msg.getContent();

            it->second->sendMessage(to_receiver);
            sender->sendMessage(to_sender);
            logger_.log("[" + getTimestamp() + "] PRIVATE: " + to_sender);
        } else {
            std::string error_msg = "\033[1;31m[System] Error: User '" + receiver + "' not found\033[0m";
            error_msg += "\n\033[1;36mAvailable users: ";

            std::lock_guard<std::mutex> lock(clients_mutex_);
            for(const auto& [nick, _] : nicknames_) {
                if(nick != sender->getNickname()) {
                    error_msg += nick + ", ";
                }
            }
            if(!nicknames_.empty()) {
                error_msg.pop_back();
                error_msg.pop_back();
            }
            error_msg += "\033[0m";

            sender->sendMessage(error_msg);
            logger_.log("[" + getTimestamp() + "] PM ERROR: " + sender->getNickname() + " tried to message " + receiver);
        }
        break;
    }

    case MessageType::NickChange: {
        std::cout << "\n--- NICK CHANGE REQUEST ---\n";
        std::cout << "From: " << sender->getNickname() << "\n";
        std::cout << "Requested nick: " << msg.getContent() << "\n";
        sender->debugInfo();

        std::string old_nick = sender->getNickname();
        std::string new_nick = msg.getContent();

        {
            std::lock_guard<std::mutex> lock(clients_mutex_);
            if(nicknames_.find(new_nick) != nicknames_.end() && new_nick != old_nick) {
                sender->sendMessage("\033[1;31m[System] Error: Nickname '" + new_nick + "' is already taken\033[0m");
                return;
            }
        }

        std::shared_ptr<ClientHandler> clientPtr;
        {
            std::lock_guard<std::mutex> lock(clients_mutex_);
            for(auto& client : clients_) {
                if(client.get() == sender) {
                    clientPtr = client;
                    break;
                }
            }
        }

        if(clientPtr) {

            {
                std::lock_guard<std::mutex> lock(clients_mutex_);
                nicknames_.erase(old_nick);
                nicknames_[new_nick] = clientPtr;
            }

            sender->setNickname(new_nick);

            std::string sys_msg = "\033[1;36m[System] " + old_nick +
                                  " changed name to\033[0m \033[1;33m" + new_nick + "\033[0m";
            broadcast(sys_msg, nullptr);


            logger_.log("[" + getTimestamp() + "] NICK CHANGE: " + old_nick + " -> " + new_nick);


        }
        std::cout << "Nick change completed: " << old_nick << " -> " << new_nick << "\n";
        break;
    }

    case MessageType::Connect:
    case MessageType::Disconnect: {
        std::string sys_msg  = "\033[1;36m[System]\033[0m " + msg.getContent();
        broadcast(sys_msg, nullptr);
        break;
    }

    case MessageType::UsersList: {
        auto users = getOnlineUsers();
        std::string userList = "\033[1;36m=== Online users (" +
                               std::to_string(users.size()) + ") ===\033[0m\n";

        for(const auto& user : users) {
            userList += " • " + user + "\n";
        }

        userList += "\033[1;36m========================\033[0m";
        sender->sendMessage(userList);
        break;
    }

    default: {
        std::cerr << "[" << getTimestamp() << "] [ERROR] Unknown message type: "
                  << static_cast<int>(msg.getType()) << std::endl;
        logger_.log("[" + getTimestamp() + "] Unknown message type from " + sender->getNickname());
    }
    }
}

void ChatServer::broadcast(const std::string& message, ClientHandler* exclude) {
    std::lock_guard<std::mutex> lock(clients_mutex_);

    std::cout << "[" << getTimestamp() << "] [BROADCAST] To " << clients_.size()
              << " clients: " << message << std::endl;

    for(auto& client : clients_) {
        if(client.get() != exclude) {
            client->sendMessage(message);
        }
    }
}
void ChatServer::privateMessage(const std::string& message, const std::string& receiver) {
    std::lock_guard<std::mutex> lock(clients_mutex_);
    auto it = nicknames_.find(receiver);
    if(it != nicknames_.end()) {
        std::cout << "[" << getTimestamp() << "] Sending PM to " << receiver << ": "
                  << message << std::endl;
        it->second->sendMessage(message);
    } else {
        std::cerr << "[" << getTimestamp() << "] PM error: Receiver not found - "
                  << receiver << std::endl;
    }
}

void ChatServer::scheduleClientRemoval(ClientHandler* client) {
    std::lock_guard<std::mutex> lock(removal_mutex_);

    if(std::find(clients_to_remove_.begin(), clients_to_remove_.end(), client) == clients_to_remove_.end()) {
        clients_to_remove_.push_back(client);
    }
}
void ChatServer::processScheduledRemovals() {
    std::lock_guard<std::mutex> lock(removal_mutex_);
    for(auto* client : clients_to_remove_) {
        clientDisconnected(client);
    }
    clients_to_remove_.clear();
}

std::vector<std::string> ChatServer::getOnlineUsers() const {
    std::lock_guard<std::mutex> lock(clients_mutex_);
    std::vector<std::string> users;
    for(const auto& [nickname, client] : nicknames_) {
        users.push_back(nickname);
    }
    return users;
}

void ChatServer::processRawMessage(ClientHandler* sender, const std::string& raw_msg) {
    if(raw_msg.empty()) return;

    if(raw_msg == "/leave") {
        std::cout << "[" << getTimestamp() << "] Client " << sender->getSocket()
                  << " (" << sender->getNickname() << ") requested to leave\n";

        sender->sendMessage("\033[1;36m[System] You are leaving the chat. Goodbye!\033[0m");

        std::this_thread::sleep_for(std::chrono::milliseconds(50));

        sender->stopClient();
        return;
    }

    if(raw_msg.rfind("/nick ", 0) == 0) {
        std::string new_nick = raw_msg.substr(6);

        size_t start = new_nick.find_first_not_of(" \t\r\n");
        size_t end = new_nick.find_last_not_of(" \t\r\n");

        if(start == std::string::npos || end == std::string::npos) {
            sender->sendMessage("\033[1;31m[System] Error: Nickname cannot be empty\033[0m");
            return;
        }
        new_nick = new_nick.substr(start, end - start + 1);

        if(new_nick.length() > 20) {
            sender->sendMessage("\033[1;31m[System] Error: Nickname too long (max 20 chars)\033[0m");
            new_nick = new_nick.substr(0, 20);
        }

        if(new_nick.find('|') != std::string::npos) {
            sender->sendMessage("\033[1;31m[System] Error: Nickname cannot contain '|' character\033[0m");
            return;
        }

        Message msg(MessageType::NickChange, sender->getNickname(), new_nick);
        processMessage(sender, msg);
        return;
    }

    if(raw_msg.rfind("/pm ", 0) == 0) {
        size_t space_pos = raw_msg.find(' ', 4);
        if(space_pos != std::string::npos) {
            std::string receiver = raw_msg.substr(4, space_pos - 4);
            std::string content = raw_msg.substr(space_pos + 1);
            Message msg(MessageType::Private, sender->getNickname(), content, receiver);
            processMessage(sender, msg);
            return;
        }
    }

    if(raw_msg == "/users") {
        Message msg(MessageType::UsersList, sender->getNickname(), "");
        processMessage(sender, msg);
        return;
    }

    Message msg(MessageType::Broadcast, sender->getNickname(), raw_msg);
    processMessage(sender, msg);
}

int ChatServer::getNextAvailableUserNumber() const {
    std::lock_guard<std::mutex> lock(clients_mutex_);
    std::set<int> usedNumbers;

    for(const auto& [nickname, _] : nicknames_) {
        if(nickname.size() >= 5 && nickname.substr(0, 4) == "User") {
            std::string numPart = nickname.substr(4);

            bool isAllDigits = !numPart.empty() &&
                               std::all_of(numPart.begin(), numPart.end(), ::isdigit);

            if(isAllDigits) {
                try {
                    int num = std::stoi(numPart);
                    usedNumbers.insert(num);
                } catch(...) {
                }
            }
        }
    }
    int candidate = 1;
    while(usedNumbers.find(candidate) != usedNumbers.end()) {
        candidate++;
    }

    return candidate;
}