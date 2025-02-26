#pragma once

#include <source_location>
#include <spdlog/spdlog.h>


namespace neon
{

struct Logger
{
    static std::shared_ptr<spdlog::logger> m_Logger;

    static void Init();


    static void test()
    {
    }
};

} // namespace neon


#define NE_LOG_WITH_SOURCE_LOCATION(loglevel, ...)              \
    std::source_location loc = std::source_location::current(); \
    neon::Logger::m_Logger->loglevel(std::format("{}:{} {}", loc.file_name(), loc.line(),  std::format("{}", __VA_ARGS__)));
    // neon::Logger::m_Logger->loglevel(std::format("{}:{}\n{}\n{}", loc.file_name(), loc.line(), loc.function_name(), std::format("{}", __VA_ARGS__)));

#define NE_TRACE(...) neon::Logger::m_Logger->trace(__VA_ARGS__)
#define NE_INFO(...) neon::Logger::m_Logger->info(__VA_ARGS__)
#define NE_WARN(...) neon::Logger::m_Logger->warn(__VA_ARGS__)
#define NE_ERROR(...) NE_LOG_WITH_SOURCE_LOCATION(error, __VA_ARGS__)
