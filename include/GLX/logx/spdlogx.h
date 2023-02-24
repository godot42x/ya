#ifndef GLX_LOGX_SPDLOG_H
#define GLX_LOGX_SPDLOG_H

#include "spdlog/common.h"
#include <spdlog/spdlog.h>

#include <ownkit/util.h>

#include <memory>
#include <string>


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
    Loggerx &InitConsoleLogger(const string &name);
    Loggerx &SetLogLvel(spdlog::level::level_enum level);

    std::shared_ptr<spdlog::logger> GetLogger() { return sp_Logger; }

  private:
    Loggerx() = default;


  private:
    std::shared_ptr<spdlog::logger> sp_Logger;
};


} // namespace logx

#define LINSTANCELOGGER logx::Loggerx::Instance().GetLogger().get()
// use embedded macro to support file and line number
#define LTRACE(...) SPDLOG_LOGGER_CALL(LINSTANCELOGGER, spdlog::level::trace, __VA_ARGS__)
#define LDEBUG(...) SPDLOG_LOGGER_CALL(LINSTANCELOGGER, spdlog::level::debug, __VA_ARGS__)
#define LINFO(...) SPDLOG_LOGGER_CALL(LINSTANCELOGGER, spdlog::level::info, __VA_ARGS__)
#define LWARN(...) SPDLOG_LOGGER_CALL(LINSTANCELOGGER, spdlog::level::warn, __VA_ARGS__)
#define LERROR(...) SPDLOG_LOGGER_CALL(LINSTANCELOGGER, spdlog::level::err, __VA_ARGS__)

#endif