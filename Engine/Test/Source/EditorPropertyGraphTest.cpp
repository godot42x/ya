#include "GameEditor/Inspector/PropertyGraph.h"
#include "GameEditor/Inspector/PropertyProjection.h"
#include "GameEditor/UI/EditorAutoPropertySection.h"
#include "Scene3D/TransformComponent.h"
#include "GUI/Widgets/WidgetTree.h"

#include <gtest/gtest.h>

namespace ya
{

TEST(EditorPropertyGraphTest, ReflectsTransformInDeclaredOrder)
{
    TransformComponent transform;
    auto graph = PropertyGraph::build(type_index_v<TransformComponent>, {&transform});

    ASSERT_EQ(graph.getNodes().size(), 3u);
    EXPECT_EQ(graph.getNodes()[0].name, "_position");
    EXPECT_EQ(graph.getNodes()[1].name, "_rotation");
    EXPECT_EQ(graph.getNodes()[2].name, "_scale");
    EXPECT_EQ(graph.getNodes()[0].displayName, "Position");
    EXPECT_TRUE(graph.getNodes()[0].bEditable);
}

TEST(EditorPropertyGraphTest, Vec3BindingReportsMixedAndWritesAllInstances)
{
    TransformComponent first;
    TransformComponent second;
    second._position.x = 3.0f;
    auto graph = PropertyGraph::build(type_index_v<TransformComponent>, {&first, &second});
    const PropertyNode* position = graph.find("_position");

    ASSERT_NE(position, nullptr);
    EXPECT_TRUE(position->binding.isMixed());
    EXPECT_TRUE(position->binding.setVec3({4.0f, 5.0f, 6.0f}));
    EXPECT_EQ(first._position, glm::vec3(4.0f, 5.0f, 6.0f));
    EXPECT_EQ(second._position, glm::vec3(4.0f, 5.0f, 6.0f));
}

TEST(EditorPropertyGraphTest, AutoPropertySectionMaterializesVec3RowsOnce)
{
    TransformComponent transform;
    auto graph = PropertyGraph::build(type_index_v<TransformComponent>, {&transform});
    auto section = std::make_shared<EditorAutoPropertySection>("AutoTransform", std::move(graph));
    WidgetTree tree({.width = 320, .height = 200});

    ASSERT_TRUE(tree.attachToLayer(WidgetTree::ELayer::Content, section).valid());
    ASSERT_EQ(section->getChildren().size(), 1u);
    EXPECT_EQ(section->getChildren()[0]->getChildren().size(), 3u);

    tree.detach(*section);
    ASSERT_TRUE(tree.attachToLayer(WidgetTree::ELayer::Content, section).valid());
    EXPECT_EQ(section->getChildren().size(), 1u);
    EXPECT_EQ(section->getChildren()[0]->getChildren().size(), 3u);
}

TEST(EditorPropertyGraphTest, TransformProjectionCustomizesDisplayNamesWithoutChangingGraphShape)
{
    TransformComponent transform;
    auto graph = PropertyGraph::build(type_index_v<TransformComponent>, {&transform});
    registerBuiltinPropertyProjections();
    PropertyProjectionRegistry::instance().apply(type_index_v<TransformComponent>, graph);

    ASSERT_EQ(graph.getNodes().size(), 3u);
    EXPECT_EQ(graph.find("_position")->displayName, "Position");
    EXPECT_EQ(graph.find("_rotation")->displayName, "Rotation");
    EXPECT_EQ(graph.find("_scale")->displayName, "Scale");
}

} // namespace ya
