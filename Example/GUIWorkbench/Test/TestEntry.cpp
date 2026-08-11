#include <gtest/gtest.h>

#include "Core/System/VirtualFileSystem.h"
#include "Core/Reflection/DeferredInitializer.h"

// GUIWorkbench workspace unit tests: the workspace is plain document /
// selection / command state, so no engine or reflection initialization is
// required.
int main(int argc, char** argv)
{
    ::testing::InitGoogleTest(&argc, argv);
    // The workspace persists documents through the virtual file system.
    VirtualFileSystem::init();
    // Execute deferred reflection registration (ClassRegistry) so
    // UIDocument field serialization works in the tests.
    ya::reflection::DeferredInitializerQueue::instance().executeAll();
    return RUN_ALL_TESTS();
}
