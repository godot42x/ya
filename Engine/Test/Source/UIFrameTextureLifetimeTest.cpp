// Snapshot draw-resource lifetime (engine suite; needs the RHI Texture type):
// the immutable frame packet holds STRONG texture references resolved at
// snapshot build time, so the asset cache may unload/clear/reload afterwards
// without invalidating this frame's GPU-safe resources.

#include "GUI/Widgets/UIFrameSnapshot.h"
#include "GUI/Widgets/WidgetTree.h"
#include "GUI/Widgets/Controls/Panel.h"

#include "RHI/Core/Texture.h"

#include <gtest/gtest.h>

#include <unordered_map>

namespace ya
{

namespace
{

/// A Texture instance requires a render device, which unit tests do not have.
/// The retention test only exercises shared_ptr mechanics (identity + strong
/// lifetime) and never dereferences the fake, so a tagged non-null pointer
/// with a no-op deleter is a faithful stand-in.
std::shared_ptr<Texture> makeFakeTexture()
{
    return std::shared_ptr<Texture>(reinterpret_cast<Texture*>(static_cast<uintptr_t>(0x1)),
                                    [](Texture*) {});
}

} // namespace

TEST(UIFrameTextureLifetimeTest, SnapshotRetainsTextureAfterCacheClear)
{
    WidgetTree tree({.width = 800, .height = 600});
    auto       panel = std::make_shared<UIPanel>("P");
    panel->setPosition({10.0f, 10.0f});
    panel->setSize({100.0f, 50.0f});

    // Fake asset cache (AssetManager's textureManager behaves the same:
    // path -> strong ref; unloading/clearing drops the cache's reference).
    std::unordered_map<std::string, std::shared_ptr<Texture>> cache;
    auto texture = makeFakeTexture();
    cache["Engine:Content/TestTextures/face.png"] = texture;
    panel->_image = TextureRef("Engine:Content/TestTextures/face.png", ya::Ptr<Texture>(texture.get()));

    tree.attachToLayer(WidgetTree::ELayer::Content, panel);

    UIFrameBuildContext ctx;
    ctx.textureResolver = [&](const std::string& path) {
        const auto it = cache.find(path);
        return it == cache.end() ? nullptr : it->second;
    };
    UIFrameSnapshot snapshot = tree.buildSnapshot(ctx);

    ASSERT_EQ(snapshot.items.size(), 1u);
    ASSERT_NE(snapshot.items[0].texture, nullptr);
    EXPECT_EQ(snapshot.items[0].texture.get(), texture.get());

    // The cache unloads/clears the texture (asset reload, scene teardown):
    // the snapshot's strong reference keeps the object alive through submit.
    cache.clear();
    texture.reset();

    ASSERT_NE(snapshot.items[0].texture, nullptr);
    EXPECT_EQ(snapshot.items[0].texture.get(),
              reinterpret_cast<Texture*>(static_cast<uintptr_t>(0x1)));
}

TEST(UIFrameTextureLifetimeTest, ResolverMissAndMissingResolverFallBackToWhite)
{
    WidgetTree tree({.width = 800, .height = 600});
    auto       panel = std::make_shared<UIPanel>("P");
    panel->setPosition({10.0f, 10.0f});
    panel->setSize({100.0f, 50.0f});
    panel->_image    = TextureRef("Engine:Content/TestTextures/face.png",
                                  ya::Ptr<Texture>(makeFakeTexture().get()));
    tree.attachToLayer(WidgetTree::ELayer::Content, panel);

    // Resolver present but cache miss: white sprite (null texture).
    UIFrameBuildContext missCtx;
    missCtx.textureResolver = [](const std::string&) { return std::shared_ptr<Texture>(); };
    const UIFrameSnapshot missSnapshot = tree.buildSnapshot(missCtx);
    ASSERT_EQ(missSnapshot.items.size(), 1u);
    EXPECT_EQ(missSnapshot.items[0].texture, nullptr);

    // No resolver at all: white sprite, never a dangling pointer.
    const UIFrameSnapshot noResolverSnapshot = tree.buildSnapshot(UIFrameBuildContext{});
    ASSERT_EQ(noResolverSnapshot.items.size(), 1u);
    EXPECT_EQ(noResolverSnapshot.items[0].texture, nullptr);
}

} // namespace ya
