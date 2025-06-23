#include "Logger.h"
#include <iostream>

template <typename T>
Logger<T>::Logger(const std::string& filename) {
    logfile_.open(filename, std::ios::app);
    if(!logfile_.is_open()) {
        throw std::runtime_error("Failed to open log file");
    }
}

template <typename T>
Logger<T>::~Logger() {
    if(logfile_.is_open()) {
        logfile_.close();
    }
}

template <typename T>
void Logger<T>::log(const T& message) {
    std::lock_guard<std::mutex> lock(mtx_);
    logfile_ << message << std::endl;
}

template class Logger<std::string>;
