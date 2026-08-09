#include "Core/Reflection/DeferredInitializer.h"
#include "Scene/Serialization/SceneSerializer.h"
#include "GUI/Scene/Node2D.h"
#include "Scene/Core/Scene.h"

#include <gtest/gtest.h>

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

} // namespace

// ============================================================================
// Reflection-driven Node2D factory: every registered Node2D subclass must be
// discoverable and creatable by short type name without a hardcoded if-chain.
// ============================================================================

TEST(Node2DFactoryTest, RegisteredTypesAreCollectedFromReflection)
{
    ensureReflectionReady();

    const std::vector<std::string> types = getRegisteredUINodeTypeNames();
    EXPECT_TRUE(std::find(types.begin(), types.end(), "UICanvasNode") != types.end());
    EXPECT_TRUE(std::find(types.begin(), types.end(), "UIPanelNode") != types.end());
    EXPECT_TRUE(std::find(types.begin(), types.end(), "UITextNode") != types.end());
    EXPECT_TRUE(std::find(types.begin(), types.end(), "UIButtonNode") != types.end());
}

TEST(Node2DFactoryTest, CreateByTypeNameReturnsTypedNodes)
{
    ensureReflectionReady();

    auto canvas = createNode2DByTypeName("UICanvasNode", "Canvas");
    ASSERT_NE(canvas, nullptr);
    EXPECT_EQ(canvas->getName(), "Canvas");
    EXPECT_NE(dynamic_cast<UICanvasNode*>(canvas.get()), nullptr);

    auto panel = createNode2DByTypeName("ya::UIPanelNode", "Panel");
    ASSERT_NE(panel, nullptr);
    EXPECT_EQ(panel->getName(), "Panel");
    EXPECT_NE(dynamic_cast<UIPanelNode*>(panel.get()), nullptr);

    auto text = createNode2DByTypeName("UITextNode", "Title");
    ASSERT_NE(text, nullptr);
    EXPECT_NE(dynamic_cast<UITextNode*>(text.get()), nullptr);

    auto button = createNode2DByTypeName("UIButtonNode", "Click");
    ASSERT_NE(button, nullptr);
    EXPECT_NE(dynamic_cast<UIButtonNode*>(button.get()), nullptr);

    EXPECT_EQ(createNode2DByTypeName("NotANodeType", "X"), nullptr);
}

// ============================================================================
// Path addressing + reparenting used by node.* script APIs.
// ============================================================================

TEST(Node2DFactoryTest, PathAddressingAndMove)
{
    ensureReflectionReady();

    Scene scene("PathScene");
    auto* canvas = scene.createUINode("UICanvasNode", "Canvas");
    ASSERT_NE(canvas, nullptr);
    auto* panel = scene.createUINode("UIPanelNode", "Panel", canvas);
    ASSERT_NE(panel, nullptr);

    EXPECT_EQ(scene.getNodePath(canvas), "/Canvas");
    EXPECT_EQ(scene.getNodePath(panel), "/Canvas/Panel");
    EXPECT_EQ(scene.findNodeByPath("/Canvas"), canvas);
    EXPECT_EQ(scene.findNodeByPath("/Canvas/Panel"), panel);
    EXPECT_EQ(scene.findNodeByPath("/Canvas/Missing"), nullptr);

    // Reparent panel to the scene root: ownership stays with the scene
    // (_entityLessNodes), only the tree parent changes.
    ASSERT_TRUE(scene.moveNode(panel, nullptr, 0));
    EXPECT_EQ(panel->getParent(), scene.getRootNode());
    EXPECT_EQ(scene.getNodePath(panel), "/Panel");

    // Move it back under the canvas.
    ASSERT_TRUE(scene.moveNode(panel, canvas, 0));
    EXPECT_EQ(scene.getNodePath(panel), "/Canvas/Panel");

    scene.destroyNode(panel);
    EXPECT_EQ(scene.findNodeByPath("/Canvas/Panel"), nullptr);
}

// ============================================================================
// Duplicate + serialize/deserialize roundtrip for Node2D.
// ============================================================================

TEST(Node2DFactoryTest, DuplicateCopiesReflectedFields)
{
    ensureReflectionReady();

    Scene scene("DupScene");
    auto* panel = scene.createUINode("UIPanelNode", "Panel");
    ASSERT_NE(panel, nullptr);
    panel->_position = {40.0f, 30.0f};
    panel->_size     = {240.0f, 120.0f};
    panel->_zOrder   = 3;
    if (auto* typed = dynamic_cast<UIPanelNode*>(panel)) {
        typed->_color = {0.12f, 0.14f, 0.22f, 0.9f};
    }

    Node* copyNode = scene.duplicateNode(panel, nullptr);
    ASSERT_NE(copyNode, nullptr);
    auto* copy = dynamic_cast<Node2D*>(copyNode);
    ASSERT_NE(copy, nullptr);
    EXPECT_NE(copy, panel);
    EXPECT_EQ(copy->getName(), "Panel Duplicate");
    EXPECT_EQ(copy->_position, panel->_position);
    EXPECT_EQ(copy->_size, panel->_size);
    EXPECT_EQ(copy->_zOrder, panel->_zOrder);
    if (auto* typedCopy = dynamic_cast<UIPanelNode*>(copy)) {
        EXPECT_EQ(typedCopy->_color, dynamic_cast<UIPanelNode*>(panel)->_color);
    }

    // Mutating the copy must not affect the source.
    copy->_position.x = 999.0f;
    EXPECT_EQ(panel->_position.x, 40.0f);
}

TEST(Node2DFactoryTest, SerializeDeserializeRoundtrip)
{
    ensureReflectionReady();

    Scene scene("RoundtripScene");
    auto* canvas = scene.createUINode("UICanvasNode", "Canvas");
    ASSERT_NE(canvas, nullptr);
    auto* panel = scene.createUINode("UIPanelNode", "Panel", canvas);
    ASSERT_NE(panel, nullptr);
    panel->_position = {10.0f, 20.0f};
    panel->_size     = {200.0f, 80.0f};
    if (auto* typed = dynamic_cast<UIPanelNode*>(panel)) {
        typed->_color = {0.3f, 0.4f, 0.5f, 1.0f};
    }

    SceneSerializer serializer(&scene);
    const nlohmann::json json = serializer.serialize();

    // Game UI no longer roundtrips through the scene tree: the canvas subtree
    // migrated to a widget entry with an inline UIDocument.
    ASSERT_TRUE(json.contains("widgetEntries"));
    const auto& entries = json["widgetEntries"];
    ASSERT_EQ(entries.size(), 1u);
    EXPECT_EQ(entries[0]["inline"]["typeId"], "engine.panel");
    EXPECT_EQ(entries[0]["inline"]["fields"]["__base__"]["UIElement"]["_position"][0], 10.0);
    EXPECT_NEAR(entries[0]["inline"]["fields"]["_color"][0].get<double>(), 0.3, 1e-6);

    Scene loadedScene("LoadedScene");
    SceneSerializer loadedSerializer(&loadedScene);
    loadedSerializer.deserialize(json);

    // The world tree stays empty of UI; the entry is authoring data.
    EXPECT_EQ(loadedScene.getRootNode()->getChildCount(), 0u);
    ASSERT_EQ(loadedScene.getWidgetEntries().size(), 1u);
    const auto& entry = loadedScene.getWidgetEntries().front();
    ASSERT_NE(entry.inlineDocument, nullptr);
    EXPECT_EQ(entry.inlineDocument->typeId, "engine.panel");
    EXPECT_EQ(entry.entryId, "Panel");
}

} // namespace ya
