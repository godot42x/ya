// GAH-301: FGuiTextureReady must run on the catalog owner thread (the thread
// that constructed FGuiTextureCatalog / WidgetTree). Catalog notify rejects
// a foreign thread before writing `cached` or Reactive dependents.
//
// Real adapters (closure-test does not link them):
//   - standalone HostGuiTextureSource: completionThread() == Caller;
//     FGuiTextureReady runs before requestLoad returns (GUIAppHost).
//   - GameRuntime AssetGuiTextureSource: default UIOwner;
//     AssetManager::loadTexture onReady is always dispatchToGameThread.

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
    std::thread::id          readyThread;
    bool                     bCompletedInline = false;

    [[nodiscard]] FGuiTextureLookup lookup(const std::string&) override
    {
        return {nullptr, EGuiTextureState::Pending};
    }

    void requestLoad(const std::string& path, FGuiTextureReady ready) override
    {
        requestThread = std::this_thread::get_id();
        readyThread = std::this_thread::get_id();
        ready(path, {texture, EGuiTextureState::Ready});
        bCompletedInline = true;
    }

    [[nodiscard]] EGuiTextureCompletionThread completionThread() const override
    {
        return EGuiTextureCompletionThread::Caller;
    }
};

struct DeferredSameThreadSource final : IGuiTextureSource
{
    std::shared_ptr<Texture> texture = makeFakeTexture();
    FGuiTextureReady         pending;
    std::string              pendingPath;
    std::thread::id          requestThread;
    std::thread::id          readyThread;

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
        readyThread = std::this_thread::get_id();
        if (!pending) {
            return;
        }
        pending(pendingPath, {texture, EGuiTextureState::Ready});
    }

    [[nodiscard]] EGuiTextureCompletionThread completionThread() const override
    {
        return EGuiTextureCompletionThread::UIOwner;
    }
};

} // namespace

TEST(TextureCompletionThreadBaselineTest, InlineSourceCompletesOnTheCallingThread)
{
    InlineSyncSource   source;
    FGuiTextureCatalog catalog;
    catalog.setSource(&source);
    EXPECT_EQ(source.completionThread(), EGuiTextureCompletionThread::Caller);
    EXPECT_EQ(catalog.ownerThread(), std::this_thread::get_id());

    const FGuiTextureLookup lookup = catalog.bind("tex:inline", {});
    EXPECT_TRUE(source.bCompletedInline);
    EXPECT_EQ(source.requestThread, std::this_thread::get_id());
    EXPECT_EQ(source.readyThread, source.requestThread);
    EXPECT_EQ(lookup.state, EGuiTextureState::Ready);
    EXPECT_EQ(lookup.texture, source.texture);
    EXPECT_EQ(catalog.foreignThreadCompletions(), 0u);
}

TEST(TextureCompletionThreadBaselineTest, DeferredSourceDoesNotCompleteInsideRequestLoad)
{
    DeferredSameThreadSource source;
    FGuiTextureCatalog       catalog;
    catalog.setSource(&source);
    EXPECT_EQ(source.completionThread(), EGuiTextureCompletionThread::UIOwner);

    const FGuiTextureLookup pending = catalog.bind("tex:deferred", {});
    EXPECT_EQ(pending.state, EGuiTextureState::Pending);
    EXPECT_EQ(source.requestThread, std::this_thread::get_id());
    EXPECT_TRUE(static_cast<bool>(source.pending));
    EXPECT_NE(source.readyThread, source.requestThread);

    source.completeOnThisThread();
    EXPECT_EQ(source.readyThread, source.requestThread);

    const FGuiTextureLookup ready = catalog.bind("tex:deferred", {});
    EXPECT_EQ(ready.state, EGuiTextureState::Ready);
    EXPECT_EQ(ready.texture, source.texture);
    EXPECT_EQ(catalog.foreignThreadCompletions(), 0u);
}

TEST(TextureCompletionThreadBaselineTest, ForeignThreadNotifyIsRejectedBeforeMutatingCatalog)
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

    EXPECT_EQ(catalog.foreignThreadCompletions(), 1u);
    EXPECT_EQ(after.wrongThreadMutations - before.wrongThreadMutations, 0u);

    const FGuiTextureLookup lookup = catalog.bind("tex:foreign", {});
    EXPECT_EQ(lookup.state, EGuiTextureState::Pending);
    EXPECT_EQ(lookup.texture, nullptr);
}

} // namespace ya
