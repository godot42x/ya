/**
 * @file StaticInitProfilerEnd.cpp  
 * @brief 静态初始化结束标记（使用 lib 段 + atexit 尽晚执行）
 */

#include "Core/Profiling/StaticInitProfiler.h"
#include <cstdlib>

#ifdef _MSC_VER
// MSVC：使用 lib 段 + atexit 确保在大多数静态变量之后执行
namespace ya::profiling::detail
{
    struct StaticInitEndMarker {
        StaticInitEndMarker() {
            // 注册退出回调，在所有静态初始化完成后记录
            std::atexit([]() { 
                StaticInitProfiler::recordEnd(); 
            });
        }
    };
}
#endif
