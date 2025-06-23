#pragma once
#include <string>
#include "MessageType.h"

class Message {
  public:
    Message() : type_(MessageType::Broadcast), sender_(""), content_(""), receiver_("") {}

    Message(MessageType type, const std::string& sender,
            const std::string& content, const std::string& receiver = "");
    MessageType getType() const;
    std::string getSender() const;
    std::string getContent() const;
    std::string getReceiver() const;
    std::string serialize() const;
    static Message deserialize(const std::string& data);

  private:
    MessageType type_;
    std::string sender_;
    std::string content_;
    std::string receiver_;
};
