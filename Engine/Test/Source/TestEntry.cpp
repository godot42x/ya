#include <gtest/gtest.h>

// Main 函数
int main(int argc, char **argv)
{
    ::testing::InitGoogleTest(&argc, argv);

    // 初始化 NameRegistry
    // 假设 NameRegistry 有静态初始化，否则需要在这里初始化

    return RUN_ALL_TESTS();
}
