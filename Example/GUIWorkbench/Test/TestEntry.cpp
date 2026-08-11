#include <gtest/gtest.h>

// GUIWorkbench workspace unit tests: the workspace is plain document /
// selection / command state, so no engine or reflection initialization is
// required.
int main(int argc, char** argv)
{
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
