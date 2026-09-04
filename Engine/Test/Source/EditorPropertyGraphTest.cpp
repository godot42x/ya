#include "GameEditor/Inspector/PropertyGraph.h"
#include "GameEditor/UI/EditorAutoPropertySection.h"
#include "GameEditor/UI/EditorAssetPicker.h"
#include "Core/Common/AssetRef.h"
#include "Core/Reflection/Reflection.h"
#include "Scene3D/TransformComponent.h"
#include "Physics/PhysicsBodyComponent.h"
#include "Render3D/Component/Material/PBRMaterialComponent.h"
#include "Render3D/Component/3D/SkyboxComponent.h"
#include "ECS/Systems/Components/TerrainComponent.h"
#include "GUI/Widgets/Controls/Button.h"
#include "GUI/Widgets/Controls/ComboBox.h"
#include "GUI/Widgets/Controls/Image.h"
#include "GUI/Widgets/Controls/InputExtras.h"
#include "GUI/Widgets/Controls/TextField.h"
#include "GUI/Binding/UndoStack.h"
#include "GUI/Widgets/WidgetTree.h"

#include <array>
#include <map>
#include <gtest/gtest.h>
#include <vector>

namespace ya
{

struct ValidationTestComponent
{
    float clamped = 5.0f;

    YA_REFLECT_BEGIN(ValidationTestComponent)
    YA_REFLECT_FIELD(clamped, .manipulate(0.0f, 10.0f, 0.5f))
    YA_REFLECT_END()
};

struct ColorTestComponent
{
    glm::vec3 tint{1.0f, 0.0f, 0.0f};
    glm::vec4 albedo{0.2f, 0.4f, 0.6f, 1.0f};
    glm::vec3 offset{1.0f, 2.0f, 3.0f};

    YA_REFLECT_BEGIN(ColorTestComponent)
    YA_REFLECT_FIELD(tint, .color())
    YA_REFLECT_FIELD(albedo, .color())
    YA_REFLECT_FIELD(offset)
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

TEST(EditorPropertyGraphTest, EnumPropertyBindsComboAndWritesAllInstances)
{
    PhysicsBodyComponent first;
    PhysicsBodyComponent second;
    second._shape = PhysicsBodyShape::Sphere;
    auto graph = PropertyGraph::build(type_index_v<PhysicsBodyComponent>, {&first, &second});
    const PropertyNode* shape = graph.find("_shape");
    ASSERT_NE(shape, nullptr);
    EXPECT_TRUE(shape->binding.isEnum());
    EXPECT_TRUE(shape->binding.isMixed());
    EXPECT_TRUE(graph.hasRetainedEditors());

    std::vector<std::string> labels;
    ASSERT_TRUE(shape->binding.enumLabels(labels));
    EXPECT_EQ(labels, (std::vector<std::string>{"Box", "Sphere"}));

    ASSERT_TRUE(shape->binding.setEnumByIndex(1));
    EXPECT_EQ(first._shape, PhysicsBodyShape::Sphere);
    EXPECT_EQ(second._shape, PhysicsBodyShape::Sphere);
}

TEST(EditorPropertyGraphTest, AutoPropertySectionEnumShowsMixedAndUndoRestoresEach)
{
    PhysicsBodyComponent first;
    PhysicsBodyComponent second;
    second._shape = PhysicsBodyShape::Sphere;
    auto graph = PropertyGraph::build(type_index_v<PhysicsBodyComponent>, {&first, &second});
    UndoStack stack;
    auto section = std::make_shared<EditorAutoPropertySection>("AutoPhysics", std::move(graph), &stack);
    WidgetTree tree({.width = 320, .height = 200});
    ASSERT_TRUE(tree.attach(*tree.getLayer(WidgetTree::ELayer::Content), section).valid());
    section->sync(tree);

    const UIElementRef& row = section->getChildren()[0]->getChildren()[0];
    auto* combo = dynamic_cast<UIComboBox*>(row->getChildren()[1].get());
    ASSERT_NE(combo, nullptr);
    EXPECT_TRUE(combo->isMixed());

    combo->select(0);
    EXPECT_EQ(first._shape, PhysicsBodyShape::Box);
    EXPECT_EQ(second._shape, PhysicsBodyShape::Box);
    EXPECT_FALSE(combo->isMixed());

    EXPECT_TRUE(stack.undo());
    EXPECT_EQ(first._shape, PhysicsBodyShape::Box);
    EXPECT_EQ(second._shape, PhysicsBodyShape::Sphere);

    tree.detach(*section);
}

TEST(EditorPropertyGraphTest, ColorPropertyReadsVec3AndVec4ThroughPropertyHandle)
{
    ColorTestComponent value;
    auto graph = PropertyGraph::build(type_index_v<ColorTestComponent>, {&value});
    const PropertyNode* tint = graph.find("tint");
    const PropertyNode* albedo = graph.find("albedo");
    const PropertyNode* offset = graph.find("offset");
    ASSERT_NE(tint, nullptr);
    ASSERT_NE(albedo, nullptr);
    ASSERT_NE(offset, nullptr);
    EXPECT_TRUE(tint->bColor);
    EXPECT_TRUE(albedo->bColor);
    EXPECT_FALSE(offset->bColor);
    EXPECT_TRUE(tint->binding.isColor());
    EXPECT_TRUE(albedo->binding.isColor());

    glm::vec4 tintColor{};
    glm::vec4 albedoColor{};
    ASSERT_TRUE(tint->binding.tryGetColor(tintColor));
    ASSERT_TRUE(albedo->binding.tryGetColor(albedoColor));
    EXPECT_EQ(tintColor, glm::vec4(1.0f, 0.0f, 0.0f, 1.0f));
    EXPECT_EQ(albedoColor, glm::vec4(0.2f, 0.4f, 0.6f, 1.0f));

    ASSERT_TRUE(tint->binding.setColor(glm::vec4(0.0f, 1.0f, 0.0f, 1.0f)));
    EXPECT_EQ(value.tint, glm::vec3(0.0f, 1.0f, 0.0f));
    EXPECT_TRUE(graph.hasRetainedEditors());
}

TEST(EditorPropertyGraphTest, AutoPropertySectionColorShowsMixedAndUndoRestoresEach)
{
    ColorTestComponent first;
    ColorTestComponent second;
    second.tint = glm::vec3(0.0f, 1.0f, 0.0f);
    auto graph = PropertyGraph::build(type_index_v<ColorTestComponent>, {&first, &second});
    UndoStack stack;
    auto section = std::make_shared<EditorAutoPropertySection>("AutoColor", std::move(graph), &stack);
    WidgetTree tree({.width = 320, .height = 200});
    ASSERT_TRUE(tree.attach(*tree.getLayer(WidgetTree::ELayer::Content), section).valid());
    section->sync(tree);

    const UIElementRef& tintRow = section->getChildren()[0]->getChildren()[0];
    auto* tintEdit = dynamic_cast<UIColorEdit*>(tintRow->getChildren()[1].get());
    ASSERT_NE(tintEdit, nullptr);
    EXPECT_TRUE(tintEdit->isMixed());

    tintEdit->setColor(glm::vec4(0.0f, 0.0f, 1.0f, 1.0f));
    EXPECT_EQ(first.tint, glm::vec3(0.0f, 0.0f, 1.0f));
    EXPECT_EQ(second.tint, glm::vec3(0.0f, 0.0f, 1.0f));
    EXPECT_FALSE(tintEdit->isMixed());

    EXPECT_TRUE(stack.undo());
    EXPECT_EQ(first.tint, glm::vec3(1.0f, 0.0f, 0.0f));
    EXPECT_EQ(second.tint, glm::vec3(0.0f, 1.0f, 0.0f));

    tree.detach(*section);
}

TEST(EditorPropertyGraphTest, RecursiveProjectionFlattensNestedMaterialPropertiesAndInstallsChangeHooks)
{
    PBRMaterialComponent material;

    auto graph = PropertyGraph::project(type_index_v<PBRMaterialComponent>, {&material});
    PropertyNode* albedo = graph.find("_params.albedo");
    PropertyNode* metallic = graph.find("_params.metallic");
    PropertyNode* albedoSlot = graph.find("_albedoSlot.textureRef");
    ASSERT_NE(albedo, nullptr);
    ASSERT_NE(metallic, nullptr);
    ASSERT_NE(albedoSlot, nullptr);
    EXPECT_TRUE(graph.hasRetainedEditors());
    EXPECT_EQ(albedo->displayName, "Params / Albedo");
    EXPECT_EQ(albedoSlot->displayName, "Albedo Slot / Texture Ref");

    EXPECT_TRUE(albedo->binding.setColor(glm::vec4(0.2f, 0.3f, 0.4f, 1.0f)));
    EXPECT_EQ(material.getParams().albedo, glm::vec3(0.2f, 0.3f, 0.4f));

    int changeHookCount = 0;
    metallic->binding.setChangeHook([&changeHookCount]() { ++changeHookCount; });
    EXPECT_TRUE(metallic->binding.setFloat(0.7f));
    EXPECT_FLOAT_EQ(material.getParams().metallic, 0.7f);
    EXPECT_EQ(changeHookCount, 1);

    EXPECT_TRUE(albedoSlot->binding.setAssetPath("Content/Textures/Nested.png"));
    EXPECT_EQ(material.getTextureSlot(EPBRMaterialTextureSlot::Albedo)->textureRef.getPath(), "Content/Textures/Nested.png");
}

TEST(EditorPropertyGraphTest, TerrainVec2AndIntegerPropertiesSupportMixedEditingAndUndo)
{
    TerrainComponent first;
    TerrainComponent second;
    second._size.x = 256.0f;
    second._gridResolution = 256;

    auto graph = PropertyGraph::project(type_index_v<TerrainComponent>, {&first, &second});
    const PropertyNode* size = graph.find("_size");
    const PropertyNode* resolution = graph.find("_gridResolution");
    ASSERT_NE(size, nullptr);
    ASSERT_NE(resolution, nullptr);
    EXPECT_TRUE(graph.hasRetainedEditors());
    EXPECT_TRUE(size->binding.isMixedVecAxis(0, 2));
    EXPECT_TRUE(resolution->binding.isMixed());

    UndoStack stack;
    auto section = std::make_shared<EditorAutoPropertySection>("AutoTerrain", std::move(graph), &stack);
    WidgetTree tree({.width = 360, .height = 220});
    ASSERT_TRUE(tree.attach(*tree.getLayer(WidgetTree::ELayer::Content), section).valid());
    section->sync(tree);

    const UIElementRef& sizeRow = section->getChildren()[0]->getChildren()[1];
    auto* sizeX = dynamic_cast<UIDragFloat*>(sizeRow->getChildren()[1].get());
    auto* sizeY = dynamic_cast<UIDragFloat*>(sizeRow->getChildren()[2].get());
    ASSERT_NE(sizeX, nullptr);
    ASSERT_NE(sizeY, nullptr);
    EXPECT_TRUE(sizeX->isMixed());
    EXPECT_FALSE(sizeY->isMixed());

    sizeX->setValue(512.0f);
    EXPECT_EQ(first._size.x, 512.0f);
    EXPECT_EQ(second._size.x, 512.0f);
    EXPECT_TRUE(stack.undo());
    EXPECT_EQ(first._size.x, 100.0f);
    EXPECT_EQ(second._size.x, 256.0f);

    section->sync(tree);
    const UIElementRef& resolutionRow = section->getChildren()[0]->getChildren()[4];
    auto* resolutionDrag = dynamic_cast<UIDragFloat*>(resolutionRow->getChildren()[1].get());
    ASSERT_NE(resolutionDrag, nullptr);
    EXPECT_TRUE(resolutionDrag->isMixed());

    resolutionDrag->setValue(384.0f);
    EXPECT_EQ(first._gridResolution, 384u);
    EXPECT_EQ(second._gridResolution, 384u);
    EXPECT_TRUE(stack.undo());
    EXPECT_EQ(first._gridResolution, 128u);
    EXPECT_EQ(second._gridResolution, 256u);

    tree.detach(*section);
}

struct AssetRefTestComponent
{
    TextureRef albedo{"Content/Textures/Albedo.png"};
    ModelRef model{"Content/Models/Test.glb"};

    YA_REFLECT_BEGIN(AssetRefTestComponent)
    YA_REFLECT_FIELD(albedo)
    YA_REFLECT_FIELD(model)
    YA_REFLECT_END()
};

TEST(EditorPropertyGraphTest, AssetRefPropertyHandleReadsWritesMixedAndResolveError)
{
    AssetRefTestComponent first;
    AssetRefTestComponent second;
    second.model.setPath("Content/Models/Other.glb");
    auto graph = PropertyGraph::build(type_index_v<AssetRefTestComponent>, {&first, &second});
    const PropertyNode* model = graph.find("model");
    ASSERT_NE(model, nullptr);
    EXPECT_TRUE(model->binding.isAssetRef());
    EXPECT_EQ(model->binding.assetRefKind(), EEditorAssetPickerKind::Model);
    EXPECT_TRUE(model->binding.isMixed());
    EXPECT_TRUE(graph.hasRetainedEditors());

    std::string path;
    ASSERT_TRUE(model->binding.tryGetAssetPath(path));
    EXPECT_EQ(path, "Content/Models/Test.glb");
    ASSERT_TRUE(model->binding.setAssetPath("Content/Models/Shared.glb"));
    EXPECT_EQ(first.model.getPath(), "Content/Models/Shared.glb");
    EXPECT_EQ(second.model.getPath(), "Content/Models/Shared.glb");

    first.albedo._resolveState = EAssetResolveState::Failed;
    EXPECT_TRUE(graph.find("albedo")->binding.hasAssetResolveError());
}

TEST(EditorPropertyGraphTest, AutoPropertySectionAssetPathCommitBrowseAndUndo)
{
    AssetRefTestComponent value;
    auto graph = PropertyGraph::build(type_index_v<AssetRefTestComponent>, {&value});
    UndoStack stack;
    EEditorAssetPickerKind requestedKind = EEditorAssetPickerKind::Texture;
    std::string requestedPath;
    auto section = std::make_shared<EditorAutoPropertySection>(
        "AutoAsset",
        std::move(graph),
        &stack,
        std::string{},
        [&](EEditorAssetPickerKind kind, std::string currentPath, std::function<void(std::string)> onPicked) {
            requestedKind = kind;
            requestedPath = currentPath;
            onPicked("Content/Textures/Picked.png");
        });
    WidgetTree tree({.width = 360, .height = 220});
    ASSERT_TRUE(tree.attach(*tree.getLayer(WidgetTree::ELayer::Content), section).valid());
    section->sync(tree);

    const UIElementRef& modelRow = section->getChildren()[0]->getChildren()[1];
    auto* pathField = dynamic_cast<UITextField*>(modelRow->getChildren()[1].get());
    auto* browse = dynamic_cast<UIButton*>(modelRow->getChildren()[2].get());
    ASSERT_NE(pathField, nullptr);
    ASSERT_NE(browse, nullptr);
    EXPECT_EQ(pathField->getText(), "Content/Models/Test.glb");

    pathField->setText("Content/Models/Typed.glb");
    if (pathField->_onCommit) {
        pathField->_onCommit(pathField->getText());
    }
    EXPECT_EQ(value.model.getPath(), "Content/Models/Typed.glb");

    if (browse->_onClick) {
        browse->_onClick();
    }
    EXPECT_EQ(requestedKind, EEditorAssetPickerKind::Model);
    EXPECT_EQ(requestedPath, "Content/Models/Typed.glb");
    EXPECT_EQ(value.model.getPath(), "Content/Textures/Picked.png");

    EXPECT_TRUE(stack.undo());
    EXPECT_EQ(value.model.getPath(), "Content/Models/Typed.glb");
    EXPECT_TRUE(stack.undo());
    EXPECT_EQ(value.model.getPath(), "Content/Models/Test.glb");

    tree.detach(*section);
}

TEST(EditorPropertyGraphTest, AutoPropertySectionAssetShowsResolveErrorState)
{
    AssetRefTestComponent value;
    value.albedo._resolveState = EAssetResolveState::Failed;
    auto graph = PropertyGraph::build(type_index_v<AssetRefTestComponent>, {&value});
    auto section = std::make_shared<EditorAutoPropertySection>("AutoAssetError", std::move(graph));
    WidgetTree tree({.width = 360, .height = 220});
    ASSERT_TRUE(tree.attach(*tree.getLayer(WidgetTree::ELayer::Content), section).valid());
    section->sync(tree);

    const UIElementRef& albedoRow = section->getChildren()[0]->getChildren()[0];
    auto* pathField = dynamic_cast<UITextField*>(albedoRow->getChildren()[1].get());
    ASSERT_NE(pathField, nullptr);
    EXPECT_TRUE(pathField->hasError());

    value.albedo._resolveState = EAssetResolveState::Ready;
    section->sync(tree);
    EXPECT_FALSE(pathField->hasError());

    tree.detach(*section);
}

struct SequenceTestComponent
{
    std::array<std::string, 2> files{"posx.hdr", "negx.hdr"};

    YA_REFLECT_BEGIN(SequenceTestComponent)
    YA_REFLECT_FIELD(files)
    YA_REFLECT_END()
};

TEST(EditorPropertyGraphTest, SequenceLeavesBindIndexedPathsAndWriteElements)
{
    SequenceTestComponent first;
    SequenceTestComponent second;
    second.files[0] = "other.hdr";

    auto graph = PropertyGraph::build(type_index_v<SequenceTestComponent>, {&first, &second});
    const PropertyNode* face0 = graph.find("files[0]");
    const PropertyNode* face1 = graph.find("files[1]");
    ASSERT_NE(face0, nullptr);
    ASSERT_NE(face1, nullptr);
    EXPECT_EQ(face0->valueType, type_index_v<std::string>);
    EXPECT_EQ(face0->displayName, "Files [0]");
    EXPECT_TRUE(face0->binding.isMixed());
    EXPECT_FALSE(face1->binding.isMixed());

    std::string value;
    ASSERT_TRUE(face0->binding.tryGetString(value));
    EXPECT_EQ(value, "posx.hdr");
    EXPECT_TRUE(face0->binding.setString("front.hdr"));
    EXPECT_EQ(first.files[0], "front.hdr");
    EXPECT_EQ(second.files[0], "front.hdr");
    EXPECT_EQ(first.files[1], "negx.hdr");
    EXPECT_EQ(second.files[1], "negx.hdr");
}

TEST(EditorPropertyGraphTest, AutoPropertySectionSequenceStringUndoRestoresEach)
{
    SequenceTestComponent first;
    SequenceTestComponent second;
    second.files[0] = "other.hdr";

    auto graph = PropertyGraph::build(type_index_v<SequenceTestComponent>, {&first, &second});
    UndoStack stack;
    auto section = std::make_shared<EditorAutoPropertySection>("AutoSequence", std::move(graph), &stack);
    WidgetTree tree({.width = 360, .height = 200});
    ASSERT_TRUE(tree.attach(*tree.getLayer(WidgetTree::ELayer::Content), section).valid());
    ASSERT_EQ(section->getChildren().size(), 1u);
    ASSERT_EQ(section->getChildren()[0]->getChildren().size(), 2u);

    const UIElementRef& row = section->getChildren()[0]->getChildren()[0];
    auto* field = dynamic_cast<UITextField*>(row->getChildren()[1].get());
    ASSERT_NE(field, nullptr);
    field->setText("front.hdr");
    if (field->_onCommit) {
        field->_onCommit(field->getText());
    }

    EXPECT_EQ(first.files[0], "front.hdr");
    EXPECT_EQ(second.files[0], "front.hdr");
    EXPECT_EQ(stack.undoCount(), 1u);
    EXPECT_TRUE(stack.undo());
    EXPECT_EQ(first.files[0], "posx.hdr");
    EXPECT_EQ(second.files[0], "other.hdr");
    EXPECT_TRUE(stack.redo());
    EXPECT_EQ(first.files[0], "front.hdr");
    EXPECT_EQ(second.files[0], "front.hdr");

    tree.detach(*section);
}

TEST(EditorPropertyGraphTest, SkyboxCubemapFilesExpandAsIndexedStringLeaves)
{
    SkyboxComponent skybox;
    skybox.cubemapSource.files[0] = "px.hdr";
    auto graph = PropertyGraph::build(type_index_v<SkyboxComponent>, {&skybox});
    const PropertyNode* face0 = graph.find("cubemapSource.files[0]");
    const PropertyNode* face5 = graph.find("cubemapSource.files[5]");
    ASSERT_NE(face0, nullptr);
    ASSERT_NE(face5, nullptr);
    EXPECT_EQ(face0->valueType, type_index_v<std::string>);
    EXPECT_EQ(face0->displayName, "Cubemap Source / Files [0]");

    std::string value;
    ASSERT_TRUE(face0->binding.tryGetString(value));
    EXPECT_EQ(value, "px.hdr");
    EXPECT_TRUE(face0->binding.setString("front.hdr"));
    EXPECT_EQ(skybox.cubemapSource.files[0], "front.hdr");
    EXPECT_TRUE(skybox.cubemapSource.files[1].empty());
}

struct DynamicSequenceComponent
{
    std::vector<std::string> tags{"alpha", "beta"};

    YA_REFLECT_BEGIN(DynamicSequenceComponent)
    YA_REFLECT_FIELD(tags)
    YA_REFLECT_END()
};

struct MapComponent
{
    std::map<std::string, int> slots{{"sword", 2}, {"shield", 1}};

    YA_REFLECT_BEGIN(MapComponent)
    YA_REFLECT_FIELD(slots)
    YA_REFLECT_END()
};

TEST(EditorPropertyGraphTest, DynamicSequenceAddRemoveRebuildsRowsAndUndo)
{
    DynamicSequenceComponent value;
    auto graph = PropertyGraph::build(type_index_v<DynamicSequenceComponent>, {&value});
    ASSERT_NE(graph.find("tags"), nullptr);
    EXPECT_EQ(graph.find("tags")->kind, PropertyNode::Kind::Sequence);
    ASSERT_NE(graph.find("tags[0]"), nullptr);
    ASSERT_NE(graph.find("tags[1]"), nullptr);

    UndoStack stack;
    auto section = std::make_shared<EditorAutoPropertySection>("AutoTags", std::move(graph), &stack);
    WidgetTree tree({.width = 360, .height = 240});
    ASSERT_TRUE(tree.attach(*tree.getLayer(WidgetTree::ELayer::Content), section).valid());
    ASSERT_EQ(section->getChildren()[0]->getChildren().size(), 3u);

    const UIElementRef& header = section->getChildren()[0]->getChildren()[0];
    auto* add = dynamic_cast<UIButton*>(header->getChildren()[1].get());
    ASSERT_NE(add, nullptr);
    if (add->_onClick) {
        add->_onClick();
    }
    EXPECT_EQ(value.tags.size(), 3u);
    EXPECT_EQ(section->getChildren()[0]->getChildren().size(), 4u);

    EXPECT_TRUE(stack.undo());
    EXPECT_EQ(value.tags.size(), 2u);

    tree.detach(*section);
}

TEST(EditorPropertyGraphTest, MapLeavesEditAndRemoveKeys)
{
    MapComponent value;
    auto graph = PropertyGraph::build(type_index_v<MapComponent>, {&value});
    ASSERT_NE(graph.find("slots"), nullptr);
    EXPECT_EQ(graph.find("slots")->kind, PropertyNode::Kind::Map);
    const PropertyNode* sword = graph.find("slots[\"sword\"]");
    ASSERT_NE(sword, nullptr);
    EXPECT_EQ(sword->valueType, type_index_v<int>);
    int64_t amount = 0;
    ASSERT_TRUE(sword->binding.tryGetInteger(amount));
    EXPECT_EQ(amount, 2);
    EXPECT_TRUE(sword->binding.setInteger(9));
    EXPECT_EQ(value.slots["sword"], 9);
    EXPECT_TRUE(sword->binding.removeMapKey());
    EXPECT_EQ(value.slots.count("sword"), 0u);
}

TEST(EditorPropertyGraphTest, TextureAssetRowShowsRetainedPreview)
{
    AssetRefTestComponent value;
    auto graph = PropertyGraph::build(type_index_v<AssetRefTestComponent>, {&value});
    auto section = std::make_shared<EditorAutoPropertySection>("AutoAssetPreview", std::move(graph));
    WidgetTree tree({.width = 360, .height = 220});
    ASSERT_TRUE(tree.attach(*tree.getLayer(WidgetTree::ELayer::Content), section).valid());
    section->sync(tree);

    const UIElementRef& albedoRow = section->getChildren()[0]->getChildren()[0];
    ASSERT_GE(albedoRow->getChildren().size(), 4u);
    auto* preview = dynamic_cast<UIImage*>(albedoRow->getChildren()[3].get());
    ASSERT_NE(preview, nullptr);
    EXPECT_EQ(preview->_assetPath, "Content/Textures/Albedo.png");

    tree.detach(*section);
}

} // namespace ya
