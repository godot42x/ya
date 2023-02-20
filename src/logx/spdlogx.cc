#include "spdlog/common.h"
#include "spdlog/spdlog.h"
#include <logx/spdlogx.h>

#include <spdlog/sinks/stdout_color_sinks.h>

namespace logx
{

void InitSpdLog()
{
    auto console_logx = spdlog::stdout_color_mt("console");
    spdlog::set_level(spdlog::level::trace);
}
} // namespace logx

#define LOG ;