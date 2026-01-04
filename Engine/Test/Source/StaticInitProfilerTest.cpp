/**
 * @file StaticInitProfilerTest.cpp
 * @brief 静态初始化性能分析器测试
 */

#include "Core/Profiling/StaticInitProfiler.h"
#include "Core/Log.h"
#include <thread>
#include <chrono>

// ============================================================================
// 模拟耗时的静态变量初始化
// ============================================================================

// 测试1：普通静态变量（不标记）
static int normalVar = []() {
    // 模拟 1ms 耗时
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
    return 42;
}();

// 测试2：标记的静态变量
static int slowVar1 = []() {
    YA_PROFILE_STATIC_INIT("TestModule::slowVar1");
    // 模拟 5ms 耗时
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
    return 100;
}();

// 测试3：另一个标记的静态变量
static int slowVar2 = []() {
    YA_PROFILE_STATIC_INIT("TestModule::slowVar2");
    // 模拟 3ms 耗时
    std::this_thread::sleep_for(std::chrono::milliseconds(3));
    return 200;
}();

// 测试4：快速初始化的变量（标记但不慢）
static int fastVar = []() {
    YA_PROFILE_STATIC_INIT("TestModule::fastVar");
    return 300;
}();

// ============================================================================
// 测试函数
// ============================================================================

namespace ya::test
{

void testStaticInitProfiler()
{
    YA_CORE_INFO("\n=== Static Init Profiler Test ===\n");
    
    // 获取总耗时
    double totalMs = profiling::StaticInitProfiler::getTotalMilliseconds();
    YA_CORE_INFO("Total static initialization time: {:.3f} ms", totalMs);
    
    // 获取单个变量记录
    auto records = profiling::StaticInitProfiler::getVariableRecords();
    YA_CORE_INFO("Tracked {} variables:", records.size());
    
    for (const auto& record : records) {
        YA_CORE_INFO("  - {}: {:.3f} ms", record.name, record.milliseconds);
    }
    
    // 验证变量已初始化
    YA_CORE_INFO("\nVariable values:");
    YA_CORE_INFO("  normalVar = {}", normalVar);
    YA_CORE_INFO("  slowVar1  = {}", slowVar1);
    YA_CORE_INFO("  slowVar2  = {}", slowVar2);
    YA_CORE_INFO("  fastVar   = {}", fastVar);
    
    YA_CORE_INFO("\n=== Test Passed ===\n");
}

} // namespace ya::test
