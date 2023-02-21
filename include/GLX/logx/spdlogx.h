#ifndef GLX_LOGX_SPDLOG_H
#define GLX_LOGX_SPDLOG_H

#include "spdlog/common.h"
#include <spdlog/sinks/rotating_file_sink.h>
#include <spdlog/spdlog.h>

#include <ownkit/util.h>

#include <memory>
#include <string>


namespace logx {

using std::string;


class Loggerx
{
  public:
    static Loggerx                        &Instace();
    inline std::shared_ptr<spdlog::logger> GetLogger() { return sp_Logger; }

    /**
     * @brief Init the logger <instance> by your configuration
     *
     * @param LogFilePath:
     * @param LoggerName ! The logger name of spdlog
     * @param Level ! The log level
     * @param MaxFileSize
     * @param MaxFileNums
     * @param bMultiSecurity  ! Whether config with multi thread security
     */
    void Init(string logFolder, string LogFilePath, string LoggerName, spdlog::level::level_enum Level, size_t MaxFileSize, size_t MaxFileNums, bool bMultiSecurity);

  private:
    Loggerx() = default;

    void setLogLvel(spdlog::level::level_enum level);

  private:
    std::shared_ptr<spdlog::logger> sp_Logger;
};

} // namespace logx

#endif