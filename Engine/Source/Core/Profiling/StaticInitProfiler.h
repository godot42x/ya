/**
 * @file StaticInitProfiler.h
 * @brief 静态变量初始化性能分析器
 *
 * 统计全局静态变量初始化的总耗时，帮助定位启动性能瓶颈。
 * 支持：
 * - 总耗时统计（从第一个静态变量初始化到最后一个）
 * - 单个变量耗时统计（可选）
 * - 跨平台（MSVC/GCC/Clang）
 */

#pragma once

#include "Core/Base.h"
#include <atomic>
#include <chrono>
#include <mutex>
#include <string>
#include <vector>


namespace ya::profiling
{

namespace detail
{
extern void refMStartSVC();
extern void refEndMSVC();
} // namespace detail

/**
 * @brief 静态初始化性能分析器
 *
 * 使用方法：
 * 1. 自动统计总耗时（无需手动调用，启动时自动输出）
 * 2. 可选：使用 YA_PROFILE_STATIC_INIT 宏标记慢变量
 */

class StaticInitProfiler
{
  public:
    // MSVC: 使用单独的 .cpp 文件来实现（Start.cpp 和 End.cpp）
    // 需要在入口点调用 refOBJ() 来确保链接
    static void refOBJ()
    {
#ifdef _MSC_VER
        detail::refMStartSVC();
        detail::refEndMSVC();
#endif
    }


    // ========================================================================
    // 总耗时统计（自动执行）
    // ========================================================================

    /// 记录静态初始化开始时间（编译器保证最早执行）
    static void recordStart();

    /// 记录静态初始化结束时间（编译器保证最晚执行）
    static void recordEnd();

    /// 获取总耗时（纳秒）
    static uint64_t getTotalNanoseconds();

    /// 获取总耗时（毫秒）
    static double getTotalMilliseconds();

    /// 打印统计报告
    static void printReport();

    // ========================================================================
    // 单个变量耗时统计（可选）
    // ========================================================================

    /// 记录单个变量初始化耗时
    static void recordVariable(const std::string &name, uint64_t nanoseconds);

    /// 获取所有变量的初始化记录
    struct VariableRecord
    {
        std::string name;
        uint64_t    nanoseconds;
        double      milliseconds;
    };
    static std::vector<VariableRecord> getVariableRecords();

  private:
    // 原子变量存储时间戳（线程安全）
    static std::atomic<uint64_t> _startTimeNs;
    static std::atomic<uint64_t> _endTimeNs;

    // 单个变量记录（需要锁保护）
    static std::vector<VariableRecord> _records;
    static std::mutex                  _recordsMutex;

    // 辅助函数：获取当前时间戳（纳秒）
    static uint64_t getNowNanoseconds();

    static std::string nowTimeString();
};

/**
 * @brief 单个静态变量计时器（RAII 封装）
 *
 * 使用示例：
 * ```cpp
 * static int slowVar = []() {
 *     YA_PROFILE_STATIC_INIT("MyClass::slowVar");
 *     // 耗时的初始化代码
 *     return heavyComputation();
 * }();
 * ```
 */
class StaticInitTimer
{
  public:
    explicit StaticInitTimer(const std::string &varName);
    ~StaticInitTimer();

  private:
    std::string                                    _varName;
    std::chrono::high_resolution_clock::time_point _start;
};

} // namespace ya::profiling


/// 标记单个静态变量的初始化（自动计时）
#define YA_PROFILE_STATIC_INIT(varName) \
    ya::profiling::StaticInitTimer __static_init_timer_##__LINE__(varName)

// ============================================================================
// 编译器特定的宏定义
//
// 注意：MSVC 下无法保证 100% 在所有静态变量之前/之后执行，
// 但对于大多数情况已足够准确（误差通常在微秒级别）
// ============================================================================

#ifdef _MSC_VER



#elif defined(__GNUC__) || defined(__clang__)
  // GCC/Clang：使用 constructor 优先级（101 = 早，65535 = 晚）
namespace ya::profiling::detail
{
__attribute__((constructor(101))) static void __static_init_start() { StaticInitProfiler::recordStart(); }
} // namespace ya::profiling::detail

namespace ya::profiling::detail
{
__attribute__((constructor(65535))) static void __static_init_end() { StaticInitProfiler::recordEnd(); }
} // namespace ya::profiling::detail

#else
  // 标准 C++ 回退方案（使用全局对象构造顺序）
namespace ya::profiling::detail
{
struct StaticInitStartMarker
{
    StaticInitStartMarker() { StaticInitProfiler::recordStart(); }
};
inline static StaticInitStartMarker __static_init_start_obj;
} // namespace ya::profiling::detail

    #define YA_STATIC_INIT_END_FUNC()
namespace ya::profiling::detail
{
struct StaticInitEndMarker
{
    ~StaticInitEndMarker() { StaticInitProfiler::recordEnd(); }
};
inline static StaticInitEndMarker __static_init_end_obj;
} // namespace ya::profiling::detail
#endif
