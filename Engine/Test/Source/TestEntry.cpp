#include <gtest/gtest.h>
#include "reflects-core/lib.h"

// Main 函数
int main(int argc, char **argv)
{
    ::testing::InitGoogleTest(&argc, argv);

    // 初始化 ClassRegistry - 执行所有延迟的静态初始化
    ClassRegistry::instance().executeAllPostStaticInitializers();

    return RUN_ALL_TESTS();
}
