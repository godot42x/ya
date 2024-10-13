#include "log.h"
#include "spdlog/logger.h"

namespace neon
{

spdlog::logger Logger::m_Logger = spdlog::logger{"Neon"};

}
