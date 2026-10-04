#include "GameEditor/Inspector/EditorComponentSectionRegistry.h"
#include "GameEditor/UI/Sections/EditorSpriteAnimationSection.h"

#include "Core/Common/AssetTypeRegistry.h"
#include "Core/Common/SpriteAnimationSet.h"
#include "Core/Reflection/DeferredInitializer.h"
#include "ECS/Entity.h"
#include "ECS/Systems/Components/LuaScriptComponent.h"
#include "GUI/Binding/UndoStack.h"
#include "GUI/Widgets/Controls/Button.h"
#include "GUI/Widgets/Controls/ComboBox.h"
#include "GUI/Widgets/Controls/Text.h"
#include "GUI/Widgets/Controls/TextField.h"
#include "GUI/Widgets/WidgetTree.h"
#include "Scene/Core/Scene.h"
#include "Scene/Serialization/SceneSerializer.h"
#include "Scene2D/Sprite2DComponent.h"
#include "Scene2D/SpriteAnimationComponent.h"
#include "Scene3D/TransformComponent.h"

#include <gtest/gtest.h>

#include <memory>
#include <string>

namespace ya
{
namespace
{

void ensureReflectionReady()
{
    static bool bInitialized = false;
    if (!bInitialized) {
        reflection::DeferredInitializerQueue::instance().executeAll();
        bInitialized = true;
    }
}

constexpr const char* kAsset = "sprite-anim-inspector";

std::shared_ptr<SpriteAnimationSet> makeSet()
{
    auto set     = std::make_shared<SpriteAnimationSet>();
    set->columns = 3;
    set->rows    = 4;
    set->clips   = {
        {.name = "idle", .frames = {1}, .fps = 1.0f, .bLoop = true},
        {.name = "walk", .frames = {0, 1, 2, 1}, .fps = 10.0f, .bLoop = true},
    };
    return set;
}

struct FFixture
{
    Scene                     scene{"SpriteAnimInspector"};
    Sprite2DComponent*        sprite    = nullptr;
    SpriteAnimationComponent* animation = nullptr;

    FFixture()
    {
        ensureReflectionReady();
        AssetTypeRegistry::get().store<SpriteAnimationSet>()->registerAsset(kAsset, makeSet());
        Node*   node   = scene.createNode3D("Hero", scene.getRootNode());
        Entity* entity = node->getEntity();
        sprite         = entity->addComponent<Sprite2DComponent>();
        animation      = entity->addComponent<SpriteAnimationComponent>();
        animation->animation = SpriteAnimationSetRef(kAsset);
        animation->clip = "idle";
        sprite->uvRect  = glm::vec4(0.1f, 0.2f, 0.3f, 0.4f);
    }
};

template <typename T>
[[nodiscard]] T* findControl(UIElement& root, std::string_view key)
{
    for (const auto& child : root.getChildren()) {
        if (!child) {
            continue;
        }
        const std::string& identity = child->_stableKey.empty() ? child->_name : child->_stableKey;
        if (identity == key) {
            if (auto* typed = dynamic_cast<T*>(child.get())) {
                return typed;
            }
        }
        if (auto* found = findControl<T>(*child, key)) {
            return found;
        }
    }
    return nullptr;
}

[[nodiscard]] std::shared_ptr<EditorSpriteAnimationSection> attachSection(WidgetTree& tree,
                                                                          FFixture& fixture,
                                                                          UndoStack* undo,
                                                                          EditorAssetPickerCallback picker = {},
                                                                          EditorRevealAssetCallback reveal = {},
                                                                          std::function<void(std::string)> openAsset = {})
{
    auto section = std::make_shared<EditorSpriteAnimationSection>(
        "AnimInspector",
        [&fixture]() { return fixture.animation; },
        undo,
        []() {},
        std::move(picker),
        std::move(reveal),
        std::move(openAsset));
    EXPECT_TRUE(tree.attach(*tree.getLayer(WidgetTree::ELayer::Content), section).valid());
    section->sync(tree);
    return section;
}

} // namespace

TEST(EditorSpriteAnimationSectionTest, ClipComboWritesThroughUndoAndOnEdit)
{
    FFixture   fixture;
    UndoStack  undo;
    WidgetTree tree({.width = 360, .height = 240});
    auto       section = attachSection(tree, fixture, &undo);
    auto*      combo   = findControl<UIComboBox>(*section, "AnimClip");
    ASSERT_NE(combo, nullptr);
    ASSERT_EQ(combo->_items.size(), 2u);
    EXPECT_EQ(combo->_items[0], "idle");
    EXPECT_EQ(combo->_items[1], "walk");
    EXPECT_EQ(combo->_selectedIndex, 0);

    ASSERT_TRUE(fixture.animation->play("idle"));
    EXPECT_TRUE(fixture.animation->isPlaying());

    combo->select(1);
    EXPECT_EQ(fixture.animation->clip, "walk");
    EXPECT_FALSE(fixture.animation->isPlaying());
    EXPECT_TRUE(fixture.animation->currentClip().empty());

    EXPECT_TRUE(undo.undo());
    EXPECT_EQ(fixture.animation->clip, "idle");

    tree.detach(*section);
}

TEST(EditorSpriteAnimationSectionTest, MissingClipStaysVisibleAndIsNotCleared)
{
    FFixture   fixture;
    fixture.animation->clip = "nope";
    WidgetTree tree({.width = 360, .height = 240});
    auto       section = attachSection(tree, fixture, nullptr);
    auto*      combo   = findControl<UIComboBox>(*section, "AnimClip");
    auto*      hint    = findControl<UIText>(*section, "AnimClipHint");
    ASSERT_NE(combo, nullptr);
    ASSERT_NE(hint, nullptr);
    EXPECT_TRUE(combo->isEnabled());
    ASSERT_GE(combo->_items.size(), 1u);
    EXPECT_EQ(combo->_items.front(), "nope");
    EXPECT_EQ(combo->currentLabel(), "nope");
    EXPECT_EQ(hint->getVisibility(), EWidgetVisibility::Visible);
    EXPECT_NE(hint->getText().find("not in"), std::string::npos);
    EXPECT_EQ(fixture.animation->clip, "nope");

    fixture.animation->animation = SpriteAnimationSetRef("sprite-anim-inspector-missing");
    section->sync(tree);
    EXPECT_FALSE(combo->isEnabled());
    EXPECT_EQ(combo->currentLabel(), "nope");
    EXPECT_NE(hint->getText().find("not loaded"), std::string::npos);
    EXPECT_EQ(fixture.animation->clip, "nope");

    tree.detach(*section);
}

TEST(EditorSpriteAnimationSectionTest, RegistryChoosesCustomSectionOrGenericFallback)
{
    registerBuiltinInspectorSections();
    auto& registry = EditorComponentSectionRegistry::instance();

    const auto* sprite = registry.find(type_index_v<SpriteAnimationComponent>);
    ASSERT_NE(sprite, nullptr);
    EXPECT_EQ(sprite->multi, EditorComponentSectionRegistry::EMultiInstance::AutoProperty);
    EXPECT_EQ(registry.choose(type_index_v<SpriteAnimationComponent>, 1),
              EditorComponentSectionRegistry::EChoice::Custom);
    EXPECT_EQ(registry.choose(type_index_v<SpriteAnimationComponent>, 2),
              EditorComponentSectionRegistry::EChoice::AutoProperty);

    const auto* scripts = registry.find(type_index_v<LuaScriptComponent>);
    ASSERT_NE(scripts, nullptr);
    EXPECT_EQ(scripts->multi, EditorComponentSectionRegistry::EMultiInstance::Skip);
    EXPECT_EQ(registry.choose(type_index_v<LuaScriptComponent>, 1),
              EditorComponentSectionRegistry::EChoice::Custom);
    EXPECT_EQ(registry.choose(type_index_v<LuaScriptComponent>, 3),
              EditorComponentSectionRegistry::EChoice::Skip);

    EXPECT_EQ(registry.find(type_index_v<TransformComponent>), nullptr);
    EXPECT_EQ(registry.choose(type_index_v<TransformComponent>, 1),
              EditorComponentSectionRegistry::EChoice::AutoProperty);

    EditorInspectorSectionHost host = sprite->make({});
    EXPECT_NE(host.widget, nullptr);
    EXPECT_TRUE(static_cast<bool>(host.sync));
    EXPECT_EQ(scripts->make({}).widget, nullptr);
}

TEST(EditorSpriteAnimationSectionTest, BrowseUsesRegisteredAssetTypeAndEditOpensThePath)
{
    FFixture   fixture;
    UndoStack  undo;
    WidgetTree tree({.width = 360, .height = 240});
    type_index_t requested = 0;
    std::string  requestedPath;
    std::string  revealed;
    std::string  opened;
    auto         section = attachSection(
        tree,
        fixture,
        &undo,
        [&](type_index_t refType, std::string currentPath, std::function<void(std::string)> onPicked) {
            requested     = refType;
            requestedPath = currentPath;
            onPicked("Content/Animations/Other.yaanim.json");
        },
        [&](std::string path) { revealed = std::move(path); },
        [&](std::string path) { opened = std::move(path); });

    auto* path   = findControl<UITextField>(*section, "AnimPath");
    auto* browse = findControl<UIButton>(*section, "AnimBrowse");
    auto* show   = findControl<UIButton>(*section, "AnimShow");
    auto* edit   = findControl<UIButton>(*section, "AnimEdit");
    ASSERT_NE(path, nullptr);
    ASSERT_NE(browse, nullptr);
    ASSERT_NE(show, nullptr);
    ASSERT_NE(edit, nullptr);
    EXPECT_EQ(path->getText(), kAsset);

    browse->onClicked.broadcast();
    EXPECT_EQ(requested, type_index_v<SpriteAnimationSetRef>);
    EXPECT_EQ(requestedPath, kAsset);
    EXPECT_EQ(fixture.animation->animation.getPath(), "Content/Animations/Other.yaanim.json");
    EXPECT_FALSE(fixture.animation->isPlaying());

    show->onClicked.broadcast();
    EXPECT_EQ(revealed, "Content/Animations/Other.yaanim.json");
    edit->onClicked.broadcast();
    EXPECT_EQ(opened, "Content/Animations/Other.yaanim.json");

    EXPECT_TRUE(undo.undo());
    EXPECT_EQ(fixture.animation->animation.getPath(), kAsset);

    tree.detach(*section);
}

TEST(EditorSpriteAnimationSectionTest, PreviewDoesNotChangeSerializedScene)
{
    FFixture         fixture;
    WidgetTree       tree({.width = 360, .height = 240});
    auto             section = attachSection(tree, fixture, nullptr);
    SceneSerializer  serializer(&fixture.scene);
    const glm::vec4  authored = fixture.sprite->uvRect;
    const nlohmann::json before = serializer.serialize();

    auto* play = findControl<UIButton>(*section, "AnimPlay");
    auto* stop = findControl<UIButton>(*section, "AnimStop");
    ASSERT_NE(play, nullptr);
    ASSERT_NE(stop, nullptr);
    play->onClicked.broadcast();
    section->tickSection(0.2f);
    EXPECT_NE(fixture.sprite->uvRect, authored);
    EXPECT_TRUE(fixture.animation->isPlaying());
    EXPECT_NE(serializer.serialize(), before);

    stop->onClicked.broadcast();
    EXPECT_FALSE(fixture.animation->isPlaying());
    EXPECT_EQ(fixture.sprite->uvRect, authored);
    EXPECT_EQ(serializer.serialize(), before);

    play->onClicked.broadcast();
    section->tickSection(0.2f);
    EXPECT_NE(fixture.sprite->uvRect, authored);
    tree.detach(*section);
    section.reset();
    EXPECT_EQ(fixture.sprite->uvRect, authored);
    EXPECT_EQ(serializer.serialize(), before);
}

TEST(EditorSpriteAnimationSectionTest, AtlasNoteHiddenWhenTheSetHasNoAtlas)
{
    FFixture   fixture;
    WidgetTree tree({.width = 360, .height = 240});
    auto       section = attachSection(tree, fixture, nullptr);
    auto*      note    = findControl<UIText>(*section, "AnimAtlasNote");
    ASSERT_NE(note, nullptr);
    EXPECT_EQ(note->getVisibility(), EWidgetVisibility::Collapsed);
    EXPECT_EQ(note->getText(), "贴图由动画集提供");
    tree.detach(*section);
}

TEST(EditorSpriteAnimationSectionTest, PreviewRestoresAtlasImagePathAndUv)
{
    FFixture fixture;
    auto     withAtlas = makeSet();
    withAtlas->atlas   = "Content/Textures/sheet.png";
    AssetTypeRegistry::get().store<SpriteAnimationSet>()->registerAsset(kAsset, withAtlas);
    fixture.sprite->image.textureRef.setPath("Content/Textures/skin.png");
    fixture.sprite->image.samplerConfig.filterMode = EFilter::Nearest;

    WidgetTree      tree({.width = 360, .height = 280});
    auto            section = attachSection(tree, fixture, nullptr);
    SceneSerializer serializer(&fixture.scene);
    const glm::vec4 authored = fixture.sprite->uvRect;
    const nlohmann::json before = serializer.serialize();

    auto* note = findControl<UIText>(*section, "AnimAtlasNote");
    auto* play = findControl<UIButton>(*section, "AnimPlay");
    auto* stop = findControl<UIButton>(*section, "AnimStop");
    ASSERT_NE(note, nullptr);
    ASSERT_NE(play, nullptr);
    ASSERT_NE(stop, nullptr);
    EXPECT_EQ(note->getVisibility(), EWidgetVisibility::Visible);
    EXPECT_EQ(note->getText(), "贴图由动画集提供");

    play->onClicked.broadcast();
    section->tickSection(0.2f);
    EXPECT_EQ(fixture.sprite->image.textureRef.getPath(), "Content/Textures/sheet.png");
    EXPECT_EQ(fixture.sprite->image.samplerConfig.filterMode, EFilter::Nearest);
    EXPECT_NE(fixture.sprite->uvRect, authored);
    EXPECT_NE(serializer.serialize(), before);

    stop->onClicked.broadcast();
    EXPECT_FALSE(fixture.animation->isPlaying());
    EXPECT_EQ(fixture.sprite->image.textureRef.getPath(), "Content/Textures/skin.png");
    EXPECT_EQ(fixture.sprite->image.samplerConfig.filterMode, EFilter::Nearest);
    EXPECT_EQ(fixture.sprite->uvRect, authored);
    EXPECT_EQ(serializer.serialize(), before);

    tree.detach(*section);
}

} // namespace ya
