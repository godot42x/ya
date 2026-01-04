/**
 * @file StaticInitProfilerStart.cpp
 * @brief 静态初始化起始标记（使用 compiler 段尽早执行）
 */

#include "Core/Profiling/StaticInitProfiler.h"

#ifdef _MSC_VER
// MSVC：使用 compiler 段确保在大多数静态变量之前初始化
namespace ya::profiling::detail
{
struct StaticInitStartMarker
{
    StaticInitStartMarker()
    {
        StaticInitProfiler::recordStart();
    }
};
} // namespace ya::profiling::detail
#endif
