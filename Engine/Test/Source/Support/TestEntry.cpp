#include <gtest/gtest.h>
#include "reflects-core/lib.h"
#include "Core/Reflection/DeferredInitializer.h"
#if defined(YA_TEST_RUNNER_INSTALLS_SCRIPT_BACKENDS)
#include "Script/Lua/LuaBackend.h"
#endif

// Main 函数
int main(int argc, char **argv)
{
    ::testing::InitGoogleTest(&argc, argv);

    // 初始化 ClassRegistry - 执行所有延迟的静态初始化
    ya::reflection::DeferredInitializerQueue::instance().executeAll();
#if defined(YA_TEST_RUNNER_INSTALLS_SCRIPT_BACKENDS)
    // The script backends install in the runner that links the script line
    // (ya-testing declares YA_TEST_RUNNER_INSTALLS_SCRIPT_BACKENDS next to
    // ya-script-lua). The closure gates compile this main without them.
    const ya::LuaScriptBackendScope scriptBackend;
#endif

    return RUN_ALL_TESTS();
}
