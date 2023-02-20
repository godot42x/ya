#include "spdlog/common.h"
#include "spdlog/sinks/rotating_file_sink.h"
#include "spdlog/spdlog.h"
#include <logx/spdlogx.h>

#include <spdlog/sinks/stdout_color_sinks.h>
#include <winnt.h>

namespace logx {

Loggerx &Loggerx::Instace()
{
    static Loggerx instance;
    return instance;
}

void Loggerx::init(string LogFilePath, string LoggerName, string Level, size_t MaxFileSize, size_t MaxFileNums, bool bMultiSecurity)
{
    try {
        if (bMultiSecurity) {
            sp_Logger = spdlog::rotating_logger_mt(LoggerName, LogFilePath, MaxFileSize, MaxFileSize);
        }
    }
    catch (...) {
    }
}

} // namespace logx

#define LOG ;