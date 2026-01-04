#pragma once

#include "log.cc/helper.h"
#include "log.cc/log.h"

#include <filesystem>



struct Logger
{
    static logcc::SyncLogger CoreLogger;
    static logcc::SyncLogger AppLogger;

    static void init();

    static logcc::SyncLogger &getLazyLog();
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


#define YA_CORE_TRACE_LZ(...) Logger::getLazyLog().log(logcc::LogLevel::Info, std::format(__VA_ARGS__))
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



struct YaFormatterV1
{
    std::string category;

    bool operator()(const logcc::Config &config, std::string &output, logcc::LogLevel::T level, std::string_view msg, const std::source_location &location)
    {
        using namespace logcc;

        std::string_view levelStr = logcc::LogLevel::toString(level);

        auto fileDetail = std::format("{}:{}", std::filesystem::path(location.file_name()).filename().string().c_str(), location.line());

        // clang-format off
        output = std::format(
            "[{}]\t{} "
            " {:<28} "
            "{}\n",
            levelStr, category,
            fileDetail,
            msg);

        // clang-format on

        return true;
    }
};

constexpr std::string_view getFileNameDetail(const std::string_view &absPath)
{
    // C:\Users\norm\1\craft\ya\Engine\Source\Core\Log.h
    auto pos = absPath.find_last_of("/\\");
    return absPath.substr(pos == std::string_view::npos ? 0 : pos + 1);
}
static_assert(getFileNameDetail(std::source_location::current().file_name()) == std::string_view("Log.h"));



struct YaFormatterV2
{
    std::string category;

    bool operator()(const logcc::Config & /*config*/, std::string &output, logcc::LogLevel::T level, std::string_view msg, const std::source_location &location)
    {
        using namespace logcc;

        output.clear();
        output.reserve(128 + msg.size() + category.size());

        std::string_view levelStr   = logcc::LogLevel::toString(level);
        auto             fileDetail = std::format("{}:{}",
                                      getFileNameDetail(location.file_name()),
                                      location.line());

        auto outputIt = std::back_inserter(output);

        std::format_to(outputIt,
                       "[{}]\t{} "
                       "{:<26} "
                       "{}\n",
                       levelStr,
                       category,
                       fileDetail,
                       msg);


        return true;
    }
};