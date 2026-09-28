// Regression guards for EditorUIDesignerSession's direct-manipulation path: when a
// preview child lives under a canvas host, drag edits must read/write the
// parent-owned slot edge consistently across multiple drags.

#include "GameEditor/EditorUIDesignerSession.h"
#include "GameEditor/EditorLayer.h"
#include "GameEditor/EditorUISlotEdit.h"
#include "GameEditor/Inspector/PropertyGraph.h"
#include "GameEditor/UI/Shell/EditorDocumentSession.h"

#include "GUI/Widgets/UIDocument.h"
#include "GUI/Widgets/UIDocumentStore.h"
#include "GUI/Widgets/UITypeIds.h"
#include "GUI/Widgets/UITypeRegistry.h"
#include "GUI/Widgets/Controls/Panel.h"
#include "GUI/Widgets/Controls/Button.h"
#include "GUI/Layout/UILayout.h"
#include "Scene/Core/SceneWidgetEntry.h"

#include <gtest/gtest.h>

#include <functional>
#include <memory>

namespace ya
{

namespace
{

/// The designer session reads both tables from its layer, so a test needs a
/// headless layer. A document is an asset now: it is published into the store
/// under a path and opened by that path (no scene and no file involved).
struct FDesignerFixture
{
    EditorDocumentRegistry documents;
    UIDocumentStore        uiDocuments;
    EditorLayer            layer{nullptr};

    static constexpr std::string_view kDocumentPath = "Test/UI/Designer.yaui";

    FDesignerFixture() { layer.bindDocumentServices(&documents, &uiDocuments); }

    /// Publish `widget`'s subtree as the fixture document.
    void publish(const UIElement& widget)
    {
        uiDocuments.put(kDocumentPath, UIDocument::fromWidget(widget));
    }
};

/// Root canvas with two fixed-size canvas children, "First" and "Second".
UIElementRef makeTwoChildCanvas()
{
    auto& registry = UITypeRegistry::instance();
    auto  root     = registry.createInstance(kTypeIdCanvasPanel);
    root->_name    = "Root";
    for (const char* name : {"First", "Second"}) {
        auto child   = registry.createInstance(kTypeIdCanvasPanel);
        child->_name = name;
        root->addDetachedChild(child, [](UIElement&, UISlot& edge) {
            FCanvasSlotArgs args;
            args.offset    = {20.0f, 30.0f};
            args.fixedSize = {80.0f, 40.0f};
            edge.as<UICanvasSlot>()->apply(args);
        });
    }
    return root;
}

nlohmann::json previewJson(const EditorUIDesignerSession& designer)
{
    return UIDocument::fromWidget(*designer.getPreviewRoot())->toJson();
}

std::optional<std::vector<size_t>> selectionPath(const EditorUIDesignerSession& designer)
{
    return designer.childPathOf(designer.getSelectedWidget());
}

/// Run one edit and require: exactly one undo step, undo restores the document
/// and selection it started from, redo restores the edited ones.
void expectOneUndoableStep(EditorUIDesignerSession& designer, const std::function<void()>& edit)
{
    (void)designer.buildPreviewSnapshot({1.0f, 1.0f}, {0.0f, 0.0f});
    const nlohmann::json before          = previewJson(designer);
    const auto           beforeSelection = selectionPath(designer);
    const size_t         steps           = designer.undoStack().undoCount();

    edit();
    const nlohmann::json after          = previewJson(designer);
    const auto           afterSelection = selectionPath(designer);
    ASSERT_NE(after, before);
    ASSERT_EQ(designer.undoStack().undoCount(), steps + 1);

    ASSERT_TRUE(designer.undoStack().undo());
    EXPECT_EQ(previewJson(designer), before);
    EXPECT_EQ(selectionPath(designer), beforeSelection);

    ASSERT_TRUE(designer.undoStack().redo());
    EXPECT_EQ(previewJson(designer), after);
    EXPECT_EQ(selectionPath(designer), afterSelection);
}

} // namespace

TEST(EditorUIDesignerSessionTest, ConsecutiveResizesUseTheCanvasSlotAsTheSourceOfTruth)
{
    auto& registry = UITypeRegistry::instance();
    auto root = registry.createInstance(kTypeIdCanvasPanel);
    auto child = registry.createInstance(kTypeIdCanvasPanel);
    ASSERT_NE(root, nullptr);
    ASSERT_NE(child, nullptr);
    root->_name  = "Root";
    child->_name = "Child";
    root->addDetachedChild(child, [](UIElement&, UISlot& edge) {
        auto* slot = edge.as<UICanvasSlot>();
        ASSERT_NE(slot, nullptr);
        FCanvasSlotArgs args;
        args.offset = {20.0f, 30.0f};
        args.fixedSize = {80.0f, 40.0f};
        slot->apply(args);
    });

    FDesignerFixture       fixture;
    EditorUIDesignerSession designer(&fixture.layer);
    fixture.publish(*root);
    designer.openDocument(FDesignerFixture::kDocumentPath);
    const UIFrameSnapshot initial = designer.buildPreviewSnapshot({1.0f, 1.0f}, {0.0f, 0.0f});
    (void)initial;
    designer.selectByChildPath({0});

    UIElement* previewChild = designer.getSelectedWidget();
    ASSERT_NE(previewChild, nullptr);

    designer.beginResize(previewChild, {0.0f, 0.0f}, EditorUIDesignerSession::kResizeHandleRight);
    ASSERT_TRUE(designer.applyDragDelta({20.0f, 0.0f}));
    designer.endDrag();
    const UIFrameSnapshot afterFirstSnapshot = designer.buildPreviewSnapshot({1.0f, 1.0f}, {0.0f, 0.0f});
    (void)afterFirstSnapshot;
    const Rect2D* afterFirst = designer.getSelectedLayoutRect();
    ASSERT_NE(afterFirst, nullptr);
    const glm::vec2 firstPos = afterFirst->pos;
    EXPECT_FLOAT_EQ(afterFirst->extent.x, 100.0f);

    designer.beginResize(previewChild, {0.0f, 0.0f}, EditorUIDesignerSession::kResizeHandleRight);
    ASSERT_TRUE(designer.applyDragDelta({10.0f, 0.0f}));
    designer.endDrag();
    const UIFrameSnapshot afterSecondSnapshot = designer.buildPreviewSnapshot({1.0f, 1.0f}, {0.0f, 0.0f});
    (void)afterSecondSnapshot;
    const Rect2D* afterSecond = designer.getSelectedLayoutRect();
    ASSERT_NE(afterSecond, nullptr);
    EXPECT_EQ(afterSecond->pos, firstPos);
    EXPECT_FLOAT_EQ(afterSecond->extent.x, 110.0f);
    EXPECT_FLOAT_EQ(afterSecond->extent.y, 40.0f);
}

TEST(EditorUIDesignerSessionTest, FindByChildPathResolvesRootAndNestedWidgets)
{
    auto& registry = UITypeRegistry::instance();
    auto  root     = registry.createInstance(kTypeIdCanvasPanel);
    auto  child    = registry.createInstance(kTypeIdCanvasPanel);
    ASSERT_NE(root, nullptr);
    ASSERT_NE(child, nullptr);
    root->_name  = "Root";
    child->_name = "Child";
    root->addDetachedChild(child);

    FDesignerFixture       fixture;
    EditorUIDesignerSession designer(&fixture.layer);
    fixture.publish(*root);
    designer.openDocument(FDesignerFixture::kDocumentPath);
    (void)designer.buildPreviewSnapshot({1.0f, 1.0f}, {0.0f, 0.0f});

    UIElement* previewRoot = designer.findByChildPath({});
    ASSERT_NE(previewRoot, nullptr);
    EXPECT_EQ(previewRoot, designer.getPreviewRoot());
    EXPECT_EQ(previewRoot->_name, "Root");

    UIElement* previewChild = designer.findByChildPath({0});
    ASSERT_NE(previewChild, nullptr);
    EXPECT_EQ(previewChild->_name, "Child");
    EXPECT_EQ(designer.findByChildPath({1}), nullptr);
}

TEST(EditorUIDesignerSessionTest, ApplyWidgetDropReordersPreviewSiblings)
{
    auto& registry = UITypeRegistry::instance();
    auto  root     = registry.createInstance(kTypeIdCanvasPanel);
    auto  first    = registry.createInstance(kTypeIdCanvasPanel);
    auto  second   = registry.createInstance(kTypeIdCanvasPanel);
    ASSERT_NE(root, nullptr);
    ASSERT_NE(first, nullptr);
    ASSERT_NE(second, nullptr);
    root->_name   = "Root";
    first->_name  = "First";
    second->_name = "Second";
    root->addDetachedChild(first);
    root->addDetachedChild(second);

    FDesignerFixture       fixture;
    EditorUIDesignerSession designer(&fixture.layer);
    fixture.publish(*root);
    designer.openDocument(FDesignerFixture::kDocumentPath);
    (void)designer.buildPreviewSnapshot({1.0f, 1.0f}, {0.0f, 0.0f});

    UIElement* previewFirst  = designer.findByChildPath({0});
    UIElement* previewSecond = designer.findByChildPath({1});
    ASSERT_NE(previewFirst, nullptr);
    ASSERT_NE(previewSecond, nullptr);
    EXPECT_EQ(previewFirst->_name, "First");
    EXPECT_EQ(previewSecond->_name, "Second");

    designer.applyWidgetDrop(previewFirst, *previewSecond, EditorUIDesignerSession::EDropPos::After);

    UIElement* after0 = designer.findByChildPath({0});
    UIElement* after1 = designer.findByChildPath({1});
    ASSERT_NE(after0, nullptr);
    ASSERT_NE(after1, nullptr);
    EXPECT_EQ(after0->_name, "Second");
    EXPECT_EQ(after1->_name, "First");
}

TEST(EditorUIDesignerSessionTest, ApplyWidgetDropIntoNestsChild)
{
    auto& registry = UITypeRegistry::instance();
    auto  root     = registry.createInstance(kTypeIdCanvasPanel);
    auto  first    = registry.createInstance(kTypeIdCanvasPanel);
    auto  second   = registry.createInstance(kTypeIdCanvasPanel);
    ASSERT_NE(root, nullptr);
    ASSERT_NE(first, nullptr);
    ASSERT_NE(second, nullptr);
    root->_name   = "Root";
    first->_name  = "First";
    second->_name = "Second";
    root->addDetachedChild(first);
    root->addDetachedChild(second);

    FDesignerFixture       fixture;
    EditorUIDesignerSession designer(&fixture.layer);
    fixture.publish(*root);
    designer.openDocument(FDesignerFixture::kDocumentPath);
    (void)designer.buildPreviewSnapshot({1.0f, 1.0f}, {0.0f, 0.0f});

    UIElement* previewRoot   = designer.getPreviewRoot();
    UIElement* previewFirst  = designer.findByChildPath({0});
    UIElement* previewSecond = designer.findByChildPath({1});
    ASSERT_NE(previewRoot, nullptr);
    ASSERT_NE(previewFirst, nullptr);
    ASSERT_NE(previewSecond, nullptr);

    designer.applyWidgetDrop(previewSecond, *previewFirst, EditorUIDesignerSession::EDropPos::Into);

    EXPECT_EQ(previewRoot->getChildren().size(), 1u);
    UIElement* nestedParent = designer.findByChildPath({0});
    UIElement* nestedChild  = designer.findByChildPath({0, 0});
    ASSERT_NE(nestedParent, nullptr);
    ASSERT_NE(nestedChild, nullptr);
    EXPECT_EQ(nestedParent->_name, "First");
    EXPECT_EQ(nestedChild->_name, "Second");
}

TEST(EditorUIDesignerSessionTest, DocumentRegistryTracksDirtyCloseAndSingleton)
{
    FDesignerFixture       fixture;
    EditorUIDesignerSession designer(&fixture.layer);

    designer.newDocument(kTypeIdCanvasPanel);
    ASSERT_TRUE(designer.hasDocument());
    ASSERT_NE(designer.documentSession(), nullptr);
    EXPECT_FALSE(designer.isDocumentDirty());
    EXPECT_TRUE(designer.documentSession()->ownsPreview());

    ASSERT_TRUE(designer.addPaletteWidget(kTypeIdCanvasPanel));
    EXPECT_TRUE(designer.isDocumentDirty());
    EXPECT_FALSE(designer.closeDocument());
    EXPECT_TRUE(designer.hasDocument());

    ASSERT_TRUE(designer.saveDocument());
    EXPECT_FALSE(designer.isDocumentDirty());
    EXPECT_TRUE(designer.closeDocument());
    EXPECT_FALSE(designer.hasDocument());
    EXPECT_EQ(fixture.documents.size(), 0u);
}

TEST(EditorUIDesignerSessionTest, OpenSceneEntrySharesDocumentSession)
{
    auto& registry = UITypeRegistry::instance();
    auto  root     = registry.createInstance(kTypeIdCanvasPanel);
    ASSERT_NE(root, nullptr);
    root->_name = "Root";

    FDesignerFixture fixture;
    fixture.publish(*root);

    SceneWidgetEntry entry;
    entry.entryId      = "hud";
    entry.documentPath = std::string(FDesignerFixture::kDocumentPath);

    EditorUIDesignerSession first(&fixture.layer);
    EditorUIDesignerSession second(&fixture.layer);
    first.openSceneEntry(entry);
    second.openSceneEntry(entry);

    ASSERT_NE(first.documentSession(), nullptr);
    EXPECT_EQ(first.documentSession(), second.documentSession());
    EXPECT_EQ(first.documentSession()->id(),
              makeEditorUIDocumentId(FDesignerFixture::kDocumentPath));
    EXPECT_TRUE(first.documentSession()->ownsPreview());
}

TEST(EditorUIDesignerSessionTest, CanvasPickingSelectsAButtonWithoutRunningItsClickHandler)
{
    // The designer is Authoring mode: a canvas click picks and manipulates, and
    // must never reach a widget's runtime click handler. That handler belongs to
    // the runtime instance (GameUIHost), which is a different tree for the same
    // document.
    auto& registry = UITypeRegistry::instance();
    auto  root     = registry.createInstance(kTypeIdCanvasPanel);
    auto  button   = registry.createInstance(kTypeIdButton);
    ASSERT_NE(root, nullptr);
    ASSERT_NE(button, nullptr);
    root->_name   = "Root";
    button->_name = "Button";

    int clicks = 0;
    auto* typed = dynamic_cast<UIButton*>(button.get());
    ASSERT_NE(typed, nullptr);
    typed->onClicked.addLambda([&clicks]() { ++clicks; });

    root->addDetachedChild(button, [](UIElement&, UISlot& edge) {
        auto* slot = edge.as<UICanvasSlot>();
        ASSERT_NE(slot, nullptr);
        FCanvasSlotArgs args;
        args.fixedSize        = {120.0f, 40.0f};
        args.widthSizeMode    = EWidgetSizeMode::Fixed;
        args.heightSizeMode   = EWidgetSizeMode::Fixed;
        args.offset           = {40.0f, 40.0f};
        ASSERT_TRUE(slot->applyArgs(args));
    });

    FDesignerFixture fixture;
    fixture.publish(*root);

    EditorUIDesignerSession designer(&fixture.layer);
    designer.openDocument(FDesignerFixture::kDocumentPath);

    UIElement* previewRoot = designer.getPreviewRoot();
    ASSERT_NE(previewRoot, nullptr);
    ASSERT_EQ(previewRoot->getChildren().size(), 1u);
    (void)designer.buildPreviewSnapshot({1.0f, 1.0f}, {0.0f, 0.0f});

    // Picking finds the button and selects it -- geometry still works.
    const Rect2D&   buttonRect = previewRoot->getChildren()[0]->_layoutRect;
    const glm::vec2 inside     = buttonRect.pos + buttonRect.extent * 0.5f;
    UIElement*      picked     = designer.pickAt(inside);
    ASSERT_NE(picked, nullptr);
    designer.select(picked);
    EXPECT_EQ(designer.getSelectedWidget(), previewRoot->getChildren()[0].get());

    // Selecting is not clicking: the preview has no dispatch path, so a canvas
    // interaction cannot become a runtime interaction.
    EXPECT_EQ(clicks, 0);
}

TEST(EditorUIDesignerSessionTest, EveryEditUndoesAndRedoesToTheSameDocumentAndSelection)
{
    auto             root = makeTwoChildCanvas();
    FDesignerFixture fixture;
    fixture.publish(*root);
    EditorUIDesignerSession designer(&fixture.layer);
    designer.openDocument(FDesignerFixture::kDocumentPath);
    ASSERT_NE(designer.getPreviewRoot(), nullptr);

    designer.selectByChildPath({0});
    expectOneUndoableStep(designer, [&designer]() {
        ASSERT_TRUE(designer.addPaletteWidget(kTypeIdButton));
    });
    expectOneUndoableStep(designer, [&designer]() {
        designer.applyWidgetDrop(designer.findByChildPath({0}),
                                 *designer.findByChildPath({1}),
                                 EditorUIDesignerSession::EDropPos::After);
    });
    expectOneUndoableStep(designer, [&designer]() {
        designer.applyWidgetDrop(designer.findByChildPath({1}),
                                 *designer.findByChildPath({0}),
                                 EditorUIDesignerSession::EDropPos::Into);
    });
    designer.selectByChildPath({0});
    expectOneUndoableStep(designer, [&designer]() {
        ASSERT_TRUE(designer.deleteWidget(designer.getSelectedWidget()));
    });
}

TEST(EditorUIDesignerSessionTest, AnUndoneDeleteReselectsTheDeletedWidget)
{
    auto             root = makeTwoChildCanvas();
    FDesignerFixture fixture;
    fixture.publish(*root);
    EditorUIDesignerSession designer(&fixture.layer);
    designer.openDocument(FDesignerFixture::kDocumentPath);

    designer.selectByChildPath({1});
    ASSERT_TRUE(designer.deleteWidget(designer.getSelectedWidget()));
    EXPECT_EQ(designer.getSelectedWidget(), nullptr);

    ASSERT_TRUE(designer.undoStack().undo());
    ASSERT_NE(designer.getSelectedWidget(), nullptr);
    EXPECT_EQ(designer.getSelectedWidget()->_name, "Second");
}

TEST(EditorUIDesignerSessionTest, ADragIsOneUndoStepAndAnUnmovedDragIsNone)
{
    auto             root = makeTwoChildCanvas();
    FDesignerFixture fixture;
    fixture.publish(*root);
    EditorUIDesignerSession designer(&fixture.layer);
    designer.openDocument(FDesignerFixture::kDocumentPath);
    (void)designer.buildPreviewSnapshot({1.0f, 1.0f}, {0.0f, 0.0f});
    const nlohmann::json original = previewJson(designer);

    UIElement* first = designer.findByChildPath({0});
    designer.beginMove(first, {0.0f, 0.0f});
    designer.endDrag();
    EXPECT_EQ(designer.undoStack().undoCount(), 0u);
    EXPECT_FALSE(designer.isDocumentDirty());

    designer.beginResize(first, {0.0f, 0.0f}, EditorUIDesignerSession::kResizeHandleRight);
    for (float dx : {5.0f, 10.0f, 15.0f}) {
        ASSERT_TRUE(designer.applyDragDelta({dx, 0.0f}));
    }
    designer.endDrag();
    EXPECT_EQ(designer.undoStack().undoCount(), 1u);
    EXPECT_TRUE(designer.isDocumentDirty());

    ASSERT_TRUE(designer.undoStack().undo());
    EXPECT_EQ(previewJson(designer), original);
}

TEST(EditorUIDesignerSessionTest, CommitsInsideOneMergeGestureCollapseToOneStep)
{
    auto             root = makeTwoChildCanvas();
    FDesignerFixture fixture;
    fixture.publish(*root);
    EditorUIDesignerSession designer(&fixture.layer);
    designer.openDocument(FDesignerFixture::kDocumentPath);

    // What an inspector drag field does: one gesture, many writes.
    designer.undoStack().beginMerge();
    for (const char* name : {"F", "Fi", "Fir"}) {
        designer.findByChildPath({0})->_name = name;
        designer.commitEdit("Rename", "uidesigner/0/name");
    }
    designer.undoStack().endMerge();
    EXPECT_EQ(designer.undoStack().undoCount(), 1u);
    EXPECT_EQ(designer.findByChildPath({0})->_name, "Fir");

    ASSERT_TRUE(designer.undoStack().undo());
    EXPECT_EQ(designer.findByChildPath({0})->_name, "First");
    ASSERT_TRUE(designer.undoStack().redo());
    EXPECT_EQ(designer.findByChildPath({0})->_name, "Fir");
}

TEST(EditorUIDesignerSessionTest, ACommitPublishesTheDocumentAndAnUnchangedCommitDoesNothing)
{
    auto             root = makeTwoChildCanvas();
    FDesignerFixture fixture;
    fixture.publish(*root);
    EditorUIDesignerSession designer(&fixture.layer);
    designer.openDocument(FDesignerFixture::kDocumentPath);
    const uint64_t revision = fixture.uiDocuments.revision(FDesignerFixture::kDocumentPath);

    designer.commitEdit("Nothing");
    EXPECT_EQ(designer.undoStack().undoCount(), 0u);
    EXPECT_FALSE(designer.isDocumentDirty());
    EXPECT_EQ(fixture.uiDocuments.revision(FDesignerFixture::kDocumentPath), revision);

    designer.findByChildPath({1})->_name = "Renamed";
    designer.commitEdit("Rename");
    EXPECT_TRUE(designer.isDocumentDirty());
    EXPECT_GT(fixture.uiDocuments.revision(FDesignerFixture::kDocumentPath), revision);
    const auto published = fixture.uiDocuments.resolve(FDesignerFixture::kDocumentPath);
    ASSERT_NE(published, nullptr);
    EXPECT_EQ(published->toJson(), previewJson(designer));
}

TEST(EditorUIDesignerSessionTest, AnUndoStepIsInertOnceItsDesignerIsGoneOrOnAnotherDocument)
{
    // Both designers share the document's undo stack; a step pushed by one
    // must not touch the other, or a document it no longer shows.
    auto             root = makeTwoChildCanvas();
    FDesignerFixture fixture;
    fixture.publish(*root);
    fixture.uiDocuments.put("Test/UI/Other.yaui", UIDocument::fromWidget(*root));

    EditorUIDesignerSession keeper(&fixture.layer);
    keeper.openDocument(FDesignerFixture::kDocumentPath);
    {
        EditorUIDesignerSession editor(&fixture.layer);
        editor.openDocument(FDesignerFixture::kDocumentPath);
        ASSERT_EQ(&editor.undoStack(), &keeper.undoStack());
        editor.findByChildPath({0})->_name = "Gone";
        editor.commitEdit("Rename");
    }
    const nlohmann::json kept = previewJson(keeper);
    ASSERT_TRUE(keeper.undoStack().undo());
    EXPECT_EQ(previewJson(keeper), kept);

    EditorUIDesignerSession mover(&fixture.layer);
    mover.openDocument(FDesignerFixture::kDocumentPath);
    mover.findByChildPath({0})->_name = "Moved";
    mover.commitEdit("Rename");
    mover.openDocument("Test/UI/Other.yaui");
    const nlohmann::json other = previewJson(mover);
    ASSERT_TRUE(keeper.undoStack().undo());
    EXPECT_EQ(previewJson(mover), other);
}

TEST(EditorUIDesignerSessionTest, ASlotEditWritesThroughTheSlotAndIsOneUndoStep)
{
    auto             root = makeTwoChildCanvas();
    FDesignerFixture fixture;
    fixture.publish(*root);
    EditorUIDesignerSession designer(&fixture.layer);
    designer.openDocument(FDesignerFixture::kDocumentPath);
    designer.selectByChildPath({0});
    const nlohmann::json original = previewJson(designer);

    // The inspector's path: a reflected write on the args copy, the change
    // hook pushes it into the slot, the section commits.
    std::unique_ptr<EditorUISlotEdit> edit = designer.editSlot(designer.getSelectedWidget());
    ASSERT_NE(edit, nullptr);
    ASSERT_EQ(edit->argsType(), type_index_v<FCanvasSlotArgs>);
    PropertyGraph graph = PropertyGraph::project(edit->argsType(), {edit->args()});
    ASSERT_TRUE(graph.hasRetainedEditors());
    PropertyNode* offset = graph.find("offset");
    ASSERT_NE(offset, nullptr);
    EditorUISlotEdit* raw = edit.get();
    offset->binding.setChangeHook([raw]() { ASSERT_TRUE(raw->push()); });
    ASSERT_TRUE(offset->binding.set(glm::vec2{64.0f, 12.0f}));
    designer.commitEdit("Slot", "uidesigner/0/slot/offset");

    const nlohmann::json edited = previewJson(designer);
    EXPECT_EQ(edited["childSlots"][0]["offset"], nlohmann::json::array({64.0f, 12.0f}));
    EXPECT_EQ(designer.undoStack().undoCount(), 1u);
    ASSERT_TRUE(designer.undoStack().undo());
    EXPECT_EQ(previewJson(designer), original);

    // Undo rebuilt the preview: the old edit's child is gone, a fresh one reads the restored slot.
    auto fresh = designer.editSlot(designer.findByChildPath({0}));
    ASSERT_NE(fresh, nullptr);
    EXPECT_EQ(static_cast<FCanvasSlotArgs*>(fresh->args())->offset, glm::vec2(20.0f, 30.0f));
}

TEST(EditorUIDesignerSessionTest, SlotEditsFollowTheParentLayoutType)
{
    auto& registry = UITypeRegistry::instance();
    auto  root     = registry.createInstance(kTypeIdCanvasPanel);
    auto  column   = registry.createInstance(kTypeIdContainer);
    auto  label    = registry.createInstance(kTypeIdText);
    ASSERT_NE(column, nullptr);
    ASSERT_NE(label, nullptr);
    column->addDetachedChild(label);
    root->addDetachedChild(column);

    FDesignerFixture fixture;
    fixture.publish(*root);
    EditorUIDesignerSession designer(&fixture.layer);
    designer.openDocument(FDesignerFixture::kDocumentPath);

    EXPECT_EQ(designer.editSlot(designer.getPreviewRoot()), nullptr) << "the root's edge is the designer host's";
    auto boxEdit = designer.editSlot(designer.findByChildPath({0, 0}));
    ASSERT_NE(boxEdit, nullptr);
    EXPECT_EQ(boxEdit->argsType(), type_index_v<FBoxSlotArgs>);
    PropertyGraph graph = PropertyGraph::project(boxEdit->argsType(), {boxEdit->args()});
    const PropertyNode* sizeRule = graph.find("sizeRule");
    ASSERT_NE(sizeRule, nullptr);
    EXPECT_TRUE(sizeRule->binding.isEnum()) << "slot enums are reflected, so the row is a combo";
}

TEST(EditorUIDesignerSessionTest, AnAnchorPresetIsOneUndoableEdit)
{
    auto             root = makeTwoChildCanvas();
    FDesignerFixture fixture;
    fixture.publish(*root);
    EditorUIDesignerSession designer(&fixture.layer);
    designer.openDocument(FDesignerFixture::kDocumentPath);
    (void)designer.buildPreviewSnapshot({1.0f, 1.0f}, {0.0f, 0.0f});
    const nlohmann::json original = previewJson(designer);

    EXPECT_FALSE(designer.applyCanvasAnchorPreset(designer.getPreviewRoot(), ECanvasAnchorPreset::Fill));
    ASSERT_TRUE(designer.applyCanvasAnchorPreset(designer.findByChildPath({1}), ECanvasAnchorPreset::BottomRight));
    const nlohmann::json edited = previewJson(designer);
    const nlohmann::json& slot  = edited["childSlots"][1];
    EXPECT_EQ(slot["anchorMin"], nlohmann::json::array({1.0f, 1.0f}));
    EXPECT_EQ(slot["pivot"], nlohmann::json::array({1.0f, 1.0f}));
    EXPECT_EQ(slot["fixedSize"], nlohmann::json::array({80.0f, 40.0f})) << "the preset keeps the laid-out size";
    EXPECT_EQ(designer.undoStack().undoCount(), 1u);

    ASSERT_TRUE(designer.undoStack().undo());
    EXPECT_EQ(previewJson(designer), original);
}

} // namespace ya
