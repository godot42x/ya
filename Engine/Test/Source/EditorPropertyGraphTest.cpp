#include "GameEditor/Inspector/PropertyGraph.h"
#include "GameEditor/UI/EditorAutoPropertySection.h"
#include "Core/Reflection/Reflection.h"
#include "Scene3D/TransformComponent.h"
#include "GUI/Binding/UndoStack.h"
#include "GUI/Widgets/Controls/InputExtras.h"
#include "GUI/Widgets/WidgetTree.h"

#include <gtest/gtest.h>

namespace ya
{

struct ValidationTestComponent
{
    float clamped = 5.0f;

    YA_REFLECT_BEGIN(ValidationTestComponent)
    YA_REFLECT_FIELD(clamped, .manipulate(0.0f, 10.0f, 0.5f))
    YA_REFLECT_END()
};

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

    ASSERT_TRUE(tree.attach(*tree.getLayer(WidgetTree::ELayer::Content), section).valid());
    ASSERT_EQ(section->getChildren().size(), 1u);
    EXPECT_EQ(section->getChildren()[0]->getChildren().size(), 3u);

    tree.detach(*section);
    ASSERT_TRUE(tree.attach(*tree.getLayer(WidgetTree::ELayer::Content), section).valid());
    EXPECT_EQ(section->getChildren().size(), 1u);
    EXPECT_EQ(section->getChildren()[0]->getChildren().size(), 3u);
}

TEST(EditorPropertyGraphTest, TransformProjectionCustomizesDisplayNamesWithoutChangingGraphShape)
{
    TransformComponent transform;
    auto graph = PropertyGraph::project(type_index_v<TransformComponent>, {&transform});

    ASSERT_EQ(graph.getNodes().size(), 3u);
    EXPECT_TRUE(graph.hasRetainedEditors());
    EXPECT_EQ(graph.find("_position")->displayName, "Position");
    EXPECT_EQ(graph.find("_rotation")->displayName, "Rotation");
    EXPECT_EQ(graph.find("_scale")->displayName, "Scale");
}

TEST(EditorPropertyGraphTest, ProjectInstallsTransformSettersThatMarkDirty)
{
    TransformComponent transform;
    transform.clearLocalDirty();
    auto built = PropertyGraph::build(type_index_v<TransformComponent>, {&transform});
    ASSERT_TRUE(built.find("_position")->binding.setVec3({1.0f, 2.0f, 3.0f}));
    EXPECT_EQ(transform._position, glm::vec3(1.0f, 2.0f, 3.0f));
    EXPECT_FALSE(transform.isLocalDirty());

    transform.clearLocalDirty();
    auto projected = PropertyGraph::project(type_index_v<TransformComponent>, {&transform});
    ASSERT_TRUE(projected.find("_position")->binding.setVec3({4.0f, 5.0f, 6.0f}));
    EXPECT_EQ(transform.getPosition(), glm::vec3(4.0f, 5.0f, 6.0f));
    EXPECT_TRUE(transform.isLocalDirty());
}

TEST(EditorPropertyGraphTest, AutoPropertySectionDragMergeUndoesToPreDragValue)
{
    TransformComponent transform;
    auto graph = PropertyGraph::project(type_index_v<TransformComponent>, {&transform});
    UndoStack stack;
    auto section = std::make_shared<EditorAutoPropertySection>("AutoTransform", std::move(graph), &stack);
    WidgetTree tree({.width = 320, .height = 200});
    ASSERT_TRUE(tree.attach(*tree.getLayer(WidgetTree::ELayer::Content), section).valid());

    ASSERT_EQ(section->getChildren().size(), 1u);
    ASSERT_FALSE(section->getChildren()[0]->getChildren().empty());
    const UIElementRef& row = section->getChildren()[0]->getChildren()[0];
    ASSERT_GE(row->getChildren().size(), 2u);
    auto* drag = dynamic_cast<UIDragFloat*>(row->getChildren()[1].get());
    ASSERT_NE(drag, nullptr);

    if (drag->_onDragBegan) {
        drag->_onDragBegan();
    }
    drag->setValue(1.0f);
    drag->setValue(4.0f);
    drag->setValue(8.0f);
    if (drag->_onDragEnded) {
        drag->_onDragEnded();
    }

    EXPECT_EQ(transform.getPosition().x, 8.0f);
    EXPECT_TRUE(transform.isLocalDirty());
    EXPECT_EQ(stack.undoCount(), 1u);
    EXPECT_TRUE(stack.undo());
    EXPECT_EQ(transform.getPosition().x, 0.0f);
    EXPECT_TRUE(stack.redo());
    EXPECT_EQ(transform.getPosition().x, 8.0f);

    tree.detach(*section);
}

TEST(EditorPropertyGraphTest, MixedVec3ShowsDashWritesAllAndUndoRestoresEach)
{
    TransformComponent first;
    TransformComponent second;
    second._position.x = 3.0f;
    auto graph = PropertyGraph::project(type_index_v<TransformComponent>, {&first, &second});
    ASSERT_TRUE(graph.find("_position")->binding.isMixed());
    EXPECT_TRUE(graph.find("_position")->binding.isMixedVec3Axis(0));
    EXPECT_FALSE(graph.find("_position")->binding.isMixedVec3Axis(1));

    UndoStack stack;
    auto section = std::make_shared<EditorAutoPropertySection>("AutoTransform", std::move(graph), &stack);
    WidgetTree tree({.width = 320, .height = 200});
    ASSERT_TRUE(tree.attach(*tree.getLayer(WidgetTree::ELayer::Content), section).valid());
    section->sync(tree);

    const UIElementRef& row = section->getChildren()[0]->getChildren()[0];
    auto* dragX = dynamic_cast<UIDragFloat*>(row->getChildren()[1].get());
    auto* dragY = dynamic_cast<UIDragFloat*>(row->getChildren()[2].get());
    ASSERT_NE(dragX, nullptr);
    ASSERT_NE(dragY, nullptr);
    EXPECT_TRUE(dragX->isMixed());
    EXPECT_FALSE(dragY->isMixed());

    dragX->setValue(9.0f);
    EXPECT_EQ(first.getPosition().x, 9.0f);
    EXPECT_EQ(second.getPosition().x, 9.0f);
    EXPECT_FALSE(dragX->isMixed());

    EXPECT_TRUE(stack.undo());
    EXPECT_EQ(first.getPosition().x, 0.0f);
    EXPECT_EQ(second.getPosition().x, 3.0f);

    tree.detach(*section);
}

TEST(EditorPropertyGraphTest, PropertyHandleReportsRangeValidationError)
{
    ValidationTestComponent value{.clamped = 12.0f};
    auto graph = PropertyGraph::build(type_index_v<ValidationTestComponent>, {&value});
    const PropertyNode* node = graph.find("clamped");
    ASSERT_NE(node, nullptr);
    EXPECT_FALSE(node->binding.validationError().empty());

    value.clamped = 4.0f;
    EXPECT_TRUE(node->binding.validationError().empty());
}

TEST(EditorPropertyGraphTest, AutoPropertySectionShowsValidationErrorState)
{
    ValidationTestComponent value{.clamped = 12.0f};
    auto graph = PropertyGraph::build(type_index_v<ValidationTestComponent>, {&value});
    auto section = std::make_shared<EditorAutoPropertySection>("Validation", std::move(graph));
    WidgetTree tree({.width = 320, .height = 200});
    ASSERT_TRUE(tree.attach(*tree.getLayer(WidgetTree::ELayer::Content), section).valid());
    section->sync(tree);

    const UIElementRef& row = section->getChildren()[0]->getChildren()[0];
    auto* drag = dynamic_cast<UIDragFloat*>(row->getChildren()[1].get());
    ASSERT_NE(drag, nullptr);
    EXPECT_TRUE(drag->hasError());
    EXPECT_FLOAT_EQ(drag->_min, 0.0f);
    EXPECT_FLOAT_EQ(drag->_max, 10.0f);

    value.clamped = 4.0f;
    section->sync(tree);
    EXPECT_FALSE(drag->hasError());

    tree.detach(*section);
}

} // namespace ya
