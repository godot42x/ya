#include "log.h"
#include "spdlog/logger.h"
#include "spdlog/sinks/stdout_color_sinks.h"
#include <memory>

namespace neon
{

std::shared_ptr<spdlog::logger> Logger::m_Logger;

void Logger::Init()
{
    Logger::m_Logger = spdlog::stdout_color_mt("Neon");
    Logger::m_Logger->set_level(spdlog::level::trace);
    Logger::m_Logger->set_pattern("%^[%H:%M:%S %z] [%n] [%L] [%t] %v%$");
}

} // namespace neon
