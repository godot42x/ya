#include "Log.h"

#include <filesystem>


logcc::SyncLogger Logger::CoreLogger;
logcc::SyncLogger Logger::AppLogger;



struct YaFormatter
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

void Logger::init()
{
    CoreLogger.setFormatter(YaFormatter("Core"));
    AppLogger.setFormatter(logcc::CategoryFormatter("App"));
}
