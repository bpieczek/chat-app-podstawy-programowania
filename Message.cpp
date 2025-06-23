#include "Message.h"
#include <sstream>
#include <vector>

Message::Message(MessageType type, const std::string& sender,
                 const std::string& content, const std::string& receiver)
    : type_(type), sender_(sender), content_(content), receiver_(receiver) {}

MessageType Message::getType() const {
    return type_;
}
std::string Message::getSender() const {
    return sender_;
}
std::string Message::getContent() const {
    return content_;
}
std::string Message::getReceiver() const {
    return receiver_;
}

std::string Message::serialize() const {
    std::ostringstream oss;
    oss << static_cast<int>(type_) << '|'
        << sender_ << '|'
        << content_ << '|'
        << receiver_;
    return oss.str();
}

Message Message::deserialize(const std::string& data) {
    size_t pos1 = data.find('|');
    if(pos1 == std::string::npos) {
        return Message(MessageType::Broadcast, "ERROR", "Invalid message format (no first separator)");
    }

    size_t pos2 = data.find('|', pos1 + 1);
    if(pos2 == std::string::npos) {
        return Message(MessageType::Broadcast, "ERROR", "Invalid message format (no second separator)");
    }

    size_t pos3 = data.find('|', pos2 + 1);

    int type = 0;
    try {
        type = std::stoi(data.substr(0, pos1));
    } catch(...) {
        return Message(MessageType::Broadcast, "ERROR", "Invalid message type");
    }

    std::string sender = data.substr(pos1 + 1, pos2 - pos1 - 1);
    std::string content = data.substr(pos2 + 1, (pos3 != std::string::npos) ? (pos3 - pos2 - 1) : (data.size() - pos2 - 1));
    std::string receiver = (pos3 != std::string::npos) ? data.substr(pos3 + 1) : "";

    return Message(static_cast<MessageType>(type), sender, content, receiver);
}
