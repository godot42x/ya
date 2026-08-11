#include <gtest/gtest.h>

// GUIWorkbench workspace unit tests: pure app-state workspace, no engine
// or reflection initialization required.
int main(int argc, char** argv)
{
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
