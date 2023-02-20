#ifndef GLX_LOGX_SPDLOG_H
#define GLX_LOGX_SPDLOG_H

#include <spdlog/sinks/rotating_file_sink.h>
#include <spdlog/spdlog.h>


#include <memory>
#include <string>

namespace logx {

using std::string;

class Loggerx
{
  public:
    static Loggerx &Instace();

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
    void init(string LogFilePath, string LoggerName, string Level, size_t MaxFileSize, size_t MaxFileNums, bool bMultiSecurity);

  private:
    Loggerx() = default;

  public:
    std::shared_ptr<spdlog::logger> sp_Logger;
};

} // namespace logx

#endif