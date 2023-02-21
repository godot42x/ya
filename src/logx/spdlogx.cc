#include <iostream>
#include <logx/spdlogx.h>

#include "spdlog/common.h"
#include "spdlog/sinks/rotating_file_sink.h"
#include "spdlog/spdlog.h"


#include <ownkit/util.h>
#include <stdexcept>



namespace logx {



Loggerx &Loggerx::Instace()
{
    static Loggerx instance;
    return instance;
}

void Loggerx::Init(const string &logFolder, const string &LogFilePath, const string &LoggerName,
                   spdlog::level::level_enum level, size_t MaxFileSize, size_t MaxFiles, bool bMultiSecurity)
{
    try
    {
        ownkit::CreateDirectoryIfNotExist(logFolder);
        if (bMultiSecurity) {
            sp_Logger = spdlog::rotating_logger_mt(LoggerName, LogFilePath,
                                                   MaxFileSize, MaxFiles);
        }
        else {
            sp_Logger = spdlog::rotating_logger_st(LoggerName, LogFilePath,
                                                   MaxFileSize, MaxFiles);
        }
        setLogLvel(level);
        sp_Logger->set_pattern("[%Y-%m-%d %H:%M:%s.%e] [%l] 【%t] [%s %!:%#] %v");
    }
    catch (const spdlog::spdlog_ex &ex) {
        std::cerr << ex.what() << std::endl;
        throw std::runtime_error("Errot when initialize spdlog");
    }
}
void Loggerx::setLogLvel(spdlog::level::level_enum level)
{
    sp_Logger->set_level(level);
}

} // namespace logx

#define LOG ;