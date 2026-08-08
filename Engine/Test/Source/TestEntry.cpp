#include <gtest/gtest.h>
#include "reflects-core/lib.h"
#include "Core/Reflection/DeferredInitializer.h"

// Main 函数
int main(int argc, char **argv)
{
    ::testing::InitGoogleTest(&argc, argv);

    // 初始化 ClassRegistry - 执行所有延迟的静态初始化
    ya::reflection::DeferredInitializerQueue::instance().executeAll();

    return RUN_ALL_TESTS();
}
