
#include "Base.h"

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
#define NE_CORE_DEBUG(...) Logger::CoreLogger.log(logcc::LogLevel::Debug, std::format(__VA_ARGS__))
#define NE_CORE_TRACE(...) Logger::CoreLogger.log(logcc::LogLevel::Trace, std::format(__VA_ARGS__))
#define NE_CORE_INFO(...)  Logger::CoreLogger.log(logcc::LogLevel::Info, std::format(__VA_ARGS__))
#define NE_CORE_WARN(...)  Logger::CoreLogger.log(logcc::LogLevel::Warn, std::format(__VA_ARGS__))
#define NE_CORE_ERROR(...) Logger::CoreLogger.log(logcc::LogLevel::Error, std::format(__VA_ARGS__))



#define NE_DEBUG(...) Logger::AppLogger.log(logcc::LogLevel::Debug, std::format(__VA_ARGS__))
#define NE_TRACE(...) Logger::AppLogger.log(logcc::LogLevel::Trace, std::format(__VA_ARGS__))
#define NE_INFO(...)  Logger::AppLogger.log(logcc::LogLevel::Info, std::format(__VA_ARGS__))
#define NE_WARN(...)  Logger::AppLogger.log(logcc::LogLevel::Warn, std::format(__VA_ARGS__))
#define NE_ERROR(...) Logger::AppLogger.log(logcc::LogLevel::Error, std::format(__VA_ARGS__))
// clang-format on


#define NE_CORE_ASSERT(condition, ...)  \
    {                                   \
        if (!!!(condition)) {           \
            NE_CORE_ERROR(__VA_ARGS__); \
            PLATFORM_BREAK();           \
        }                               \
    }

#define NE_ASSERT(condition, ...)  \
    {                              \
        if (!!!(condition)) {      \
            NE_ERROR(__VA_ARGS__); \
            PLATFORM_BREAK();      \
        }                          \
    }