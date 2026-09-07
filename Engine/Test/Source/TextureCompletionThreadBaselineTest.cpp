// GAH-003: CPU-only baseline for GUI texture completion threads.
//
// Real adapters (not linked here):
//   - GameRuntime AssetGuiTextureSource -> AssetManager::loadTexture onReady
//     is always AssetManager::dispatchToGameThread (see AssetTextureManager).
//   - standalone HostGuiTextureSource calls FGuiTextureReady before
//     requestLoad returns (GUIAppHost).
//
// IGuiTextureSource does not declare a completion thread. Reactive already
// rejects set() off the UI thread, but catalog.notify still writes `cached`
// first. GAH-301 must refuse foreign-thread notify before mutating the entry.

#include "GUI/Binding/Reactive.h"
#include "GUI/Widgets/GuiTextureCatalog.h"
#include "RHI/Core/Texture.h"

#include <gtest/gtest.h>

#include <thread>
#include <utility>

namespace ya
{
namespace
{

std::shared_ptr<Texture> makeFakeTexture()
{
    return std::shared_ptr<Texture>(reinterpret_cast<Texture*>(static_cast<uintptr_t>(0x1)),
                                    [](Texture*) {});
}

struct InlineSyncSource final : IGuiTextureSource
{
    std::shared_ptr<Texture> texture = makeFakeTexture();
    std::thread::id          requestThread;
    std::thread::id          completionThread;
    bool                     bCompletedInline = false;

    [[nodiscard]] FGuiTextureLookup lookup(const std::string&) override
    {
        return {nullptr, EGuiTextureState::Pending};
    }

    void requestLoad(const std::string& path, FGuiTextureReady ready) override
    {
        requestThread = std::this_thread::get_id();
        completionThread = std::this_thread::get_id();
        ready(path, {texture, EGuiTextureState::Ready});
        bCompletedInline = true;
    }
};

struct DeferredSameThreadSource final : IGuiTextureSource
{
    std::shared_ptr<Texture> texture = makeFakeTexture();
    FGuiTextureReady         pending;
    std::string              pendingPath;
    std::thread::id          requestThread;
    std::thread::id          completionThread;

    [[nodiscard]] FGuiTextureLookup lookup(const std::string&) override
    {
        return {nullptr, EGuiTextureState::Pending};
    }

    void requestLoad(const std::string& path, FGuiTextureReady ready) override
    {
        requestThread = std::this_thread::get_id();
        pendingPath   = path;
        pending       = std::move(ready);
    }

    void completeOnThisThread()
    {
        completionThread = std::this_thread::get_id();
        if (!pending) {
            return;
        }
        pending(pendingPath, {texture, EGuiTextureState::Ready});
    }
};

} // namespace

TEST(TextureCompletionThreadBaselineTest, InlineSourceCompletesOnTheCallingThread)
{
    InlineSyncSource   source;
    FGuiTextureCatalog catalog;
    catalog.setSource(&source);

    const FGuiTextureLookup lookup = catalog.bind("tex:inline", {});
    EXPECT_TRUE(source.bCompletedInline);
    EXPECT_EQ(source.requestThread, std::this_thread::get_id());
    EXPECT_EQ(source.completionThread, source.requestThread);
    EXPECT_EQ(lookup.state, EGuiTextureState::Ready);
    EXPECT_EQ(lookup.texture, source.texture);
}

TEST(TextureCompletionThreadBaselineTest, DeferredSourceDoesNotCompleteInsideRequestLoad)
{
    DeferredSameThreadSource source;
    FGuiTextureCatalog       catalog;
    catalog.setSource(&source);

    const FGuiTextureLookup pending = catalog.bind("tex:deferred", {});
    EXPECT_EQ(pending.state, EGuiTextureState::Pending);
    EXPECT_EQ(source.requestThread, std::this_thread::get_id());
    EXPECT_TRUE(static_cast<bool>(source.pending));
    EXPECT_NE(source.completionThread, source.requestThread);

    source.completeOnThisThread();
    EXPECT_EQ(source.completionThread, source.requestThread);

    const FGuiTextureLookup ready = catalog.bind("tex:deferred", {});
    EXPECT_EQ(ready.state, EGuiTextureState::Ready);
    EXPECT_EQ(ready.texture, source.texture);
}

TEST(TextureCompletionThreadBaselineTest, ForeignThreadNotifyCurrentlyMutatesCatalog)
{
    Reactive<int> pinUiThread(0);
    pinUiThread.set(1);

    FGuiTextureCatalog catalog;
    auto               texture = makeFakeTexture();
    const auto         before  = getReactiveDiagnostics();
    std::thread        worker([&]() {
        catalog.notify("tex:foreign", {texture, EGuiTextureState::Ready});
    });
    worker.join();
    const auto after = getReactiveDiagnostics();

    EXPECT_EQ(after.wrongThreadMutations - before.wrongThreadMutations, 1u);

    const FGuiTextureLookup lookup = catalog.bind("tex:foreign", {});
    EXPECT_EQ(lookup.state, EGuiTextureState::Ready);
    EXPECT_EQ(lookup.texture, texture);
}

} // namespace ya
