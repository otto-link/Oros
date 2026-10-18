#include <oros/logger.hpp>

namespace oros
{

// --- Static member initialization

std::shared_ptr<spdlog::logger> Logger::instance = nullptr;

// --- Logger instance retrieval

std::shared_ptr<spdlog::logger> &Logger::log()
{
  if (!instance)
  {
    instance = spdlog::stdout_color_mt("console_oros");
    instance->set_pattern("[oros--] [%H:%M:%S] [%^---%L---%$] %v");
    instance->set_level(spdlog::level::trace);
  }

  return instance;
}

} // namespace oros
