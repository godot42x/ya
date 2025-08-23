#pragma once
#include "Base.h"

#include "log.cc/helper.h"
#include "log.cc/log.h"



struct Logger
{
    static logcc::SyncLogger CoreLogger;
    static logcc::SyncLogger AppLogger;

    static void init();
};


// Debug = 100,
// Trace = 200,
// Info  = 300,
// Warn  = 400,
// Error = 500,
// Fatal = 600,


// clang-format off
#define YA_CORE_DEBUG(...) Logger::CoreLogger.log(logcc::LogLevel::Debug, std::format(__VA_ARGS__))
#define YA_CORE_TRACE(...) Logger::CoreLogger.log(logcc::LogLevel::Trace, std::format(__VA_ARGS__))
#define YA_CORE_INFO(...)  Logger::CoreLogger.log(logcc::LogLevel::Info, std::format(__VA_ARGS__))
#define YA_CORE_WARN(...)  Logger::CoreLogger.log(logcc::LogLevel::Warn, std::format(__VA_ARGS__))
#define YA_CORE_ERROR(...) Logger::CoreLogger.log(logcc::LogLevel::Error, std::format(__VA_ARGS__))



#define YA_DEBUG(...) Logger::AppLogger.log(logcc::LogLevel::Debug, std::format(__VA_ARGS__))
#define YA_TRACE(...) Logger::AppLogger.log(logcc::LogLevel::Trace, std::format(__VA_ARGS__))
#define YA_INFO(...)  Logger::AppLogger.log(logcc::LogLevel::Info, std::format(__VA_ARGS__))
#define YA_WARN(...)  Logger::AppLogger.log(logcc::LogLevel::Warn, std::format(__VA_ARGS__))
#define YA_ERROR(...) Logger::AppLogger.log(logcc::LogLevel::Error, std::format(__VA_ARGS__))
// clang-format on


#define YA_CORE_ASSERT(condition, ...)  \
    {                                   \
        if (!!!(condition)) {           \
            YA_CORE_ERROR(__VA_ARGS__); \
            PLATFORM_BREAK();           \
        }                               \
    }

#define YA_ASSERT(condition, ...)  \
    {                              \
        if (!!!(condition)) {      \
            YA_ERROR(__VA_ARGS__); \
            PLATFORM_BREAK();      \
        }                          \
    }

#define YA_ENSURE(condition, ...) (!!(condition)) ? 1 : (YA_CORE_ERROR(__VA_ARGS__), PLATFORM_BREAK(), 0)