#include "ClientHandler.h"
#include "ChatServer.h"
#include "Message.h"
#include <unistd.h>
#include <cstring>
#include <iostream>
#include <sstream>
#include <algorithm>
#include <iomanip>
#include <ctime>

static std::string getTimestamp() {
    auto now = std::chrono::system_clock::now();
    auto in_time_t = std::chrono::system_clock::to_time_t(now);
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                  now.time_since_epoch()) % 1000;

    std::tm bt;
    localtime_r(&in_time_t, &bt);

    std::ostringstream oss;
    oss << std::put_time(&bt, "%Y-%m-%d %H:%M:%S");
    oss << '.' << std::setfill('0') << std::setw(3) << ms.count();
    return oss.str();
}

ClientHandler::ClientHandler(int socket, ChatServer* server, const std::string& defaultNickname)
    : client_socket_(socket), server_(server), active_(true), nickname_(defaultNickname) {
}
ClientHandler::~ClientHandler() {
    stopClient();
    if(thread_ && thread_->joinable()) {
        thread_->join();
    }
}

void ClientHandler::clearLine() {
    const char clear_seq[] = "\r\033[K";

    if(send(client_socket_, clear_seq, sizeof(clear_seq) - 1, 0) < 0) {
        std::cerr << "[" << getTimestamp() << "] Failed to clear line for client "
                  << client_socket_ << std::endl;
    }
}

void ClientHandler::start() {
    thread_ = std::make_unique<std::thread>(&ClientHandler::run, this);
}
void ClientHandler::run() {
    std::cout << "[" << getTimestamp() << "] Client handler started for socket: "
              << client_socket_ << std::endl;

    char buffer[1024];
    try {
        while(active_) {
            sendPrompt();

            memset(buffer, 0, sizeof(buffer));
            fd_set read_fds;
            FD_ZERO(&read_fds);
            FD_SET(client_socket_, &read_fds);

            timeval timeout;
            timeout.tv_sec = 1;
            timeout.tv_usec = 0;

            int ready = select(client_socket_ + 1, &read_fds, nullptr, nullptr, &timeout);

            if(!active_) break;

            if(ready < 0) {
                if(errno == EINTR) continue;
                std::cerr << "[" << getTimestamp() << "] select error for client "
                          << client_socket_ << ": " << strerror(errno) << std::endl;
                break;
            }

            if(ready == 0) continue;

            ssize_t bytes_received = recv(client_socket_, buffer, sizeof(buffer) - 1, 0);

            if(bytes_received <= 0) {
                if(bytes_received == 0) {
                    std::cout << "[" << getTimestamp() << "] Client " << client_socket_
                              << " (" << nickname_ << ") disconnected" << std::endl;
                } else {
                    if(errno == ECONNRESET) {
                        std::cout << "[" << getTimestamp() << "] Client " << client_socket_
                                  << " (" << nickname_ << ") force disconnected (Ctrl+C)\n";
                    } else {
                        std::cerr << "[" << getTimestamp() << "] recv error from client " << client_socket_
                                  << " (" << nickname_ << "): " << strerror(errno) << std::endl;
                    }
                }
                active_ = false;
                break;
            }

            buffer[bytes_received] = '\0';
            std::string raw_msg(buffer);

            raw_msg.erase(std::remove(raw_msg.begin(), raw_msg.end(), '\n'), raw_msg.end());
            raw_msg.erase(std::remove(raw_msg.begin(), raw_msg.end(), '\r'), raw_msg.end());

            if(!raw_msg.empty()) {
                const char clear_line[] = "\r\033[K";
                send(client_socket_, clear_line, sizeof(clear_line) - 1, MSG_NOSIGNAL);

                if(raw_msg == "/leave") {
                    sendMessage("\033[1;36m[System] You are leaving the chat. Goodbye!\033[0m");
                    std::this_thread::sleep_for(std::chrono::milliseconds(50));
                    stopClient();
                    break;
                }

                server_->processRawMessage(this, raw_msg);
                prompt_pending_ = true;
            }
        }
    } catch(const std::exception& e) {
        std::cerr << "[" << getTimestamp() << "] Exception in client handler for socket "
                  << client_socket_ << ": " << e.what() << std::endl;
    }

    stopClient();
    server_->scheduleClientRemoval(this);

    std::cout << "[" << getTimestamp() << "] Client handler exiting for socket: "
              << client_socket_ << std::endl;
}

void ClientHandler::sendMessage(const std::string& msg) {
    std::lock_guard<std::mutex> lock(socket_mutex_);

    std::string formatted = "\r" + msg + "\n";

    ssize_t bytes_sent = send(client_socket_, formatted.c_str(), formatted.size(), MSG_NOSIGNAL);
    if(bytes_sent < 0) {
        if(errno != EPIPE && errno != ECONNRESET) {
            std::cerr << "[" << getTimestamp() << "] [ERROR] send() failed: "
                      << strerror(errno) << "\n";
        }
    }

    prompt_pending_ = true;
}

void ClientHandler::handleMessage(const std::string& msg) {
    try {
        Message message = Message::deserialize(msg);
        server_->processMessage(this, message);
    } catch(const std::exception& e) {
        std::string error_msg = "[System] Error: " + std::string(e.what());
        sendMessage(error_msg);
    }
}
std::string ClientHandler::getNickname() const {
    return nickname_;
}

void ClientHandler::setNickname(const std::string& nickname) {
    nickname_ = nickname;
}

void ClientHandler::sendPrompt() {
    std::lock_guard<std::mutex> lock(socket_mutex_);

    if(!prompt_pending_) return;

    const char prompt[] = "\033[1;32m> \033[0m";
    ssize_t bytes_sent = send(client_socket_, prompt, sizeof(prompt) - 1, MSG_NOSIGNAL);

    if(bytes_sent > 0) {
        prompt_pending_ = false;
    }
}