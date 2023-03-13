#pragma once

#include <pch/gl.h>
#include <pch/std.h>

#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/spdlog.h>

#include <ownkit/util.hpp>



namespace logx {

using std::string;

enum class LoggerType
{
    Console = 0,
    File,
    Rotating, // Will rm old logs after tems of times
    Daily,
    TypeCount
};


const std::string Parttern = "[%Y-%m-%d %H:%M:%S.%e] [%n] [%l] [%@] %v %$";

class Loggerx
{
  public:
    static Loggerx &Instance();

    // TODO: I just need a logger with detail. Add more {LoggerType} later
    Loggerx &InitConsoleLogger(const string &DefLoggerName, const string &ErrLoggerName);
    Loggerx &SetLogLvel(spdlog::level::level_enum level);

    std::shared_ptr<spdlog::logger> GetLogger() { return sp_Logger; }
    std::shared_ptr<spdlog::logger> GetErrLogger() { return sp_ErrLogger; }

  private:
    Loggerx() = default;

    void setLogLvel(spdlog::level::level_enum level);

  private:
    std::shared_ptr<spdlog::logger> sp_Logger;
    std::shared_ptr<spdlog::logger> sp_ErrLogger;
};


} // namespace logx

#define DEFLOGGER logx::Loggerx::Instance().GetLogger().get()
#define ERRLOGGER logx::Loggerx::Instance().GetErrLogger().get()

// use embedded macro to support file and line number
#define LTRACE(...) SPDLOG_LOGGER_CALL(DEFLOGGER, spdlog::level::trace, __VA_ARGS__)
#define LDEBUG(...) SPDLOG_LOGGER_CALL(DEFLOGGER, spdlog::level::debug, __VA_ARGS__)
#define LINFO(...) SPDLOG_LOGGER_CALL(DEFLOGGER, spdlog::level::info, __VA_ARGS__)

#define LWARN(...) SPDLOG_LOGGER_CALL(ERRLOGGER, spdlog::level::warn, __VA_ARGS__)
#define LERROR(...) SPDLOG_LOGGER_CALL(ERRLOGGER, spdlog::level::err, __VA_ARGS__)


namespace logx {


INL Loggerx &Loggerx::Instance()
{
    static Loggerx instance;
    return instance;
}

/*
 void Loggerx::Init(const string &logFolder, const string &LogFilePath, const string &LoggerName,
                    spdlog::level::level_enum level, size_t MaxFileSize, size_t MaxFiles, bool bMultiSecurity)
 {
     try
     {
         ownkit::CreateDirectoryIfNotExist(logFolder);
         if (bMultiSecurity) {
             sp_Logger = spdlog::rotating_logger_mt(
                 LoggerName, LogFilePath,
                 MaxFileSize, MaxFiles);
         }
         else {
             sp_Logger = spdlog::rotating_logger_st(
                 LoggerName, LogFilePath,
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
*/

INL Loggerx &Loggerx::InitConsoleLogger(const string &out, const string &err)
{
    sp_Logger    = spdlog::stdout_color_mt(out);
    sp_ErrLogger = spdlog::stderr_color_mt(err);
    spdlog::set_pattern(Parttern);
    return Instance();
}

INL Loggerx &Loggerx::SetLogLvel(spdlog::level::level_enum level)
{
    sp_Logger->set_level(level);
    sp_ErrLogger->set_level(level);
    return Instance();
}

} // namespace logx