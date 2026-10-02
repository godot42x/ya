#include "Resource/AssetManager.h"

#include <gtest/gtest.h>

#include <functional>
#include <memory>

namespace ya
{
namespace
{

// No render installed: loadTexture posts onReady through the frame-task sink
// (or runs it inline once the sink is cleared) without decoding.
TEST(AssetManagerFrameTaskSink, ClearedSinkIsNotInvoked)
{
    AssetManager::get()->setRender(nullptr);

    int sinkCalls = 0;
    AssetManager::setFrameTaskSink([&sinkCalls](std::function<void()> task) {
        ++sinkCalls;
        task();
    });

    int ready = 0;
    AssetManager::get()->loadTexture(AssetManager::TextureLoadRequest{
        .filepath = "Content/Textures/__frame_sink_probe.png",
        .onReady  = [&ready](const std::shared_ptr<Texture>&) { ++ready; },
    });
    EXPECT_EQ(sinkCalls, 1);
    EXPECT_EQ(ready, 1);

    AssetManager::setFrameTaskSink({});

    int readyAfterClear = 0;
    AssetManager::get()->loadTexture(AssetManager::TextureLoadRequest{
        .filepath = "Content/Textures/__frame_sink_probe.png",
        .onReady  = [&readyAfterClear](const std::shared_ptr<Texture>&) { ++readyAfterClear; },
    });
    EXPECT_EQ(sinkCalls, 1);
    EXPECT_EQ(readyAfterClear, 1);
}

} // namespace
} // namespace ya
