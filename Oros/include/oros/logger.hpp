#pragma once

#include <memory>

#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/spdlog.h>

namespace oros
{

// --- Logger class

class Logger
{
public:
  static std::shared_ptr<spdlog::logger> &log();

private:
  Logger() = default;
  Logger(const Logger &) = delete;
  Logger &operator=(const Logger &) = delete;

  static std::shared_ptr<spdlog::logger> instance;
};

} // namespace oros
