#pragma once

#include "Core/Api.h"
#include "Core/Base.h" // PLATFORM_BREAK() used by YA_CORE_ASSERT/YA_ASSERT/YA_ENSURE

#include "log.cc/helper.h"
#include "log.cc/log.h"

#include <filesystem>



struct Logger
{
    static YA_CORE_API void init();

    static YA_CORE_API logcc::SyncLogger& core();
    static YA_CORE_API logcc::SyncLogger& app();
    static YA_CORE_API logcc::SyncLogger& getLazyLog();
};


// Debug = 100,
// Trace = 200,
// Info  = 300,
// Warn  = 400,
// Error = 500,
// Fatal = 600,


// clang-format off
#define YA_CORE_DEBUG(...) Logger::core().log(logcc::LogLevel::Debug, std::format(__VA_ARGS__))
#define YA_CORE_TRACE(...) Logger::core().log(logcc::LogLevel::Trace, std::format(__VA_ARGS__))
#define YA_CORE_INFO(...)  Logger::core().log(logcc::LogLevel::Info, std::format(__VA_ARGS__))
#define YA_CORE_WARN(...)  Logger::core().log(logcc::LogLevel::Warn, std::format(__VA_ARGS__))
#define YA_CORE_ERROR(...) Logger::core().log(logcc::LogLevel::Error, std::format(__VA_ARGS__))



#define YA_DEBUG(...) Logger::app().log(logcc::LogLevel::Debug, std::format(__VA_ARGS__))
#define YA_TRACE(...) Logger::app().log(logcc::LogLevel::Trace, std::format(__VA_ARGS__))
#define YA_INFO(...)  Logger::app().log(logcc::LogLevel::Info, std::format(__VA_ARGS__))
#define YA_WARN(...)  Logger::app().log(logcc::LogLevel::Warn, std::format(__VA_ARGS__))
#define YA_ERROR(...) Logger::app().log(logcc::LogLevel::Error, std::format(__VA_ARGS__))


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

        std::string_view levelStr   = logcc::LogLevel::toString(level);
        auto             fileDetail = std::format("{}:{}",
                                      getFileNameDetail(location.file_name()),
                                      location.line());
        output = std::format("[{}]\t{} "
                             "{:<26} "
                             "{}\n",
                             levelStr,
                             category,
                             fileDetail,
                             msg);

        return true;
    }
};
