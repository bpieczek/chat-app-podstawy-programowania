#pragma once
#include <fstream>
#include <mutex>
#include <string>

template <typename T>
class Logger {
  public:
    Logger(const std::string& filename);
    ~Logger();

    void log(const T& message);

  private:
    std::ofstream logfile_;
    std::mutex mtx_;
};
