#pragma once

#include "spdlog/common.h"
#include <spdlog/spdlog.h>

namespace neon
{

struct Logger
{
    static spdlog::logger m_Logger;
};



#define NE_LOG(...) Logger::m_Logger.info(__VA_ARGS__)
#define NE_WARN(...) Logger::m_Logger.warn(__VA_ARGS__)
#define NE_ERROR(...) Logger::m_Logger.error(__VA_ARGS__)

} // namespace neon