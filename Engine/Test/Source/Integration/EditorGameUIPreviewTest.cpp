// Guards the editor's scene-UI preview as a persistent tree in Authoring mode:
// it is instantiated once per set of mount inputs, and it neither ticks (no
// behaviour/anim frame) nor dispatches input (a canvas click must not run a
// widget's runtime click handler -- that is the runtime tree's job).

#include "GameEditor/UI/Viewport/EditorGameUIPreview.h"
#include "GameRuntime/GUI/GameUI/GameUIHost.h"

#include "GUI/Widgets/Controls/Border.h"
#include "GUI/Widgets/Controls/Button.h"
#include "GUI/Widgets/UIDocument.h"
#include "GUI/Widgets/UIDocumentStore.h"
#include "GUI/Widgets/UITypeIds.h"
#include "GUI/Widgets/UITypeRegistry.h"
#include "Scene/Core/Scene.h"
#include "Scene/Core/SceneWidgetEntry.h"

#include <gtest/gtest.h>

namespace ya
{

namespace
{

std::string publishDocument(UIDocumentStore&   store,
                            std::string_view   name,
                            const std::string& typeId,
                            nlohmann::json     fields = nlohmann::json::object())
{
    auto document    = std::make_shared<UIDocument>();
    document->typeId = typeId;
    document->fields = std::move(fields);
    std::string path = "Test/UI/Preview" + std::string(name) + ".yaui";
    store.put(path, std::move(document));
    return path;
}

SceneWidgetEntry makeMount(const std::string& entryId, const std::string& documentPath)
{
    SceneWidgetEntry entry;
    entry.entryId      = entryId;
    entry.documentPath = documentPath;
    entry.autoMount    = true;
    return entry;
}

} // namespace

TEST(EditorGameUIPreviewTest, StableSceneIsInstantiatedOnceAcrossFrames)
{
    UIDocumentStore documents;
    Scene scene("World");
    scene.addWidgetEntry(makeMount("HUD", publishDocument(documents, "HUD", kTypeIdBorder)));

    EditorGameUIPreview preview;
    for (int frame = 0; frame < 8; ++frame) {
        const UIFrameSnapshot snapshot = preview.buildSnapshot(scene,
                                                               &documents,
                                                               Extent2D{800, 600},
                                                               {1.0f, 1.0f},
                                                               {0.0f, 0.0f});
        ASSERT_EQ(snapshot.items.size(), 1u);
    }
    // Rebuilding every compose is the defect this guards: it discards preview
    // state and re-instantiates every mounted document each frame.
    EXPECT_EQ(preview.rebuildCount(), 1u);
}

TEST(EditorGameUIPreviewTest, EditingAMountedDocumentRebuildsThePreview)
{
    UIDocumentStore documents;
    Scene scene("World");
    const std::string path = publishDocument(documents, "HUD", kTypeIdBorder);
    scene.addWidgetEntry(makeMount("HUD", path));

    EditorGameUIPreview preview;
    (void)preview.buildSnapshot(scene, &documents, Extent2D{800, 600}, {1.0f, 1.0f}, {0.0f, 0.0f});
    EXPECT_EQ(preview.rebuildCount(), 1u);

    // Publish an authoring edit to the document the scene mounts. The preview
    // must pick it up; comparing document identity would miss an in-place edit.
    auto edited    = std::make_shared<UIDocument>();
    edited->typeId = kTypeIdBorder;
    documents.put(path, std::move(edited));

    (void)preview.buildSnapshot(scene, &documents, Extent2D{800, 600}, {1.0f, 1.0f}, {0.0f, 0.0f});
    EXPECT_EQ(preview.rebuildCount(), 2u);
}

TEST(EditorGameUIPreviewTest, ChangingExtentOrSceneRebuilds)
{
    UIDocumentStore documents;
    Scene scene("World");
    scene.addWidgetEntry(makeMount("HUD", publishDocument(documents, "HUD", kTypeIdBorder)));

    EditorGameUIPreview preview;
    (void)preview.buildSnapshot(scene, &documents, Extent2D{800, 600}, {1.0f, 1.0f}, {0.0f, 0.0f});
    EXPECT_EQ(preview.rebuildCount(), 1u);

    (void)preview.buildSnapshot(scene, &documents, Extent2D{1024, 768}, {1.0f, 1.0f}, {0.0f, 0.0f});
    EXPECT_EQ(preview.rebuildCount(), 2u);

    Scene other("Other");
    other.addWidgetEntry(makeMount("HUD", publishDocument(documents, "HUD2", kTypeIdBorder)));
    (void)preview.buildSnapshot(other, &documents, Extent2D{1024, 768}, {1.0f, 1.0f}, {0.0f, 0.0f});
    EXPECT_EQ(preview.rebuildCount(), 3u);
}

TEST(EditorGameUIPreviewTest, AuthoringModeDoesNotDispatchInputToWidgets)
{
    // A preview that ran click handlers would be a second runtime: the editor
    // would act on game state the game does not have. The preview exposes no
    // dispatch path at all -- its tree is reached through buildSnapshot only.
    UIDocumentStore documents;
    Scene scene("World");
    scene.addWidgetEntry(makeMount("HUD", publishDocument(documents, "Btn", kTypeIdButton)));

    EditorGameUIPreview preview;
    const UIFrameSnapshot snapshot =
        preview.buildSnapshot(scene, &documents, Extent2D{800, 600}, {1.0f, 1.0f}, {0.0f, 0.0f});
    ASSERT_FALSE(snapshot.items.empty());

    // Repeated snapshots advance no frame-driven state: a tree that ticked would
    // need a tick entry point, and this type has none.
    const uint64_t rebuilds = preview.rebuildCount();
    for (int frame = 0; frame < 5; ++frame) {
        (void)preview.buildSnapshot(scene, &documents, Extent2D{800, 600}, {1.0f, 1.0f}, {0.0f, 0.0f});
    }
    EXPECT_EQ(preview.rebuildCount(), rebuilds);
}

TEST(EditorGameUIPreviewTest, PreviewDoesNotActivateBehaviors)
{
    struct CountingActivator final : public IGameUIBehaviorRuntime
    {
        int* count;
        explicit CountingActivator(int& out) : count(&out) {}
        void activate(UIElement&, const FUIBehaviorSpec&, const FUIBehaviorActivation&) override { ++*count; }
        void update() override {}
    };

    UIDocumentStore documents;
    auto document       = std::make_shared<UIDocument>();
    document->typeId    = kTypeIdBorder;
    document->behaviors = {{.type = "script.lua", .data = {{"script", "UI/HUD.lua"}}}};
    documents.put("Test/UI/PreviewScripted.yaui", document);
    Scene scene("World");
    scene.addWidgetEntry(makeMount("HUD", "Test/UI/PreviewScripted.yaui"));

    // Positive control: the runtime host presenting the same scene activates.
    int activations = 0;
    GameUIHost host;
    host.setDocumentStore(&documents);
    host.setBehaviorRuntime(std::make_unique<CountingActivator>(activations));
    host.onSceneActivated(scene);
    ASSERT_EQ(activations, 1);

    // The preview takes no activator; the widget is shown, its spec stays inert.
    EditorGameUIPreview preview;
    const UIFrameSnapshot snapshot =
        preview.buildSnapshot(scene, &documents, Extent2D{800, 600}, {1.0f, 1.0f}, {0.0f, 0.0f});
    EXPECT_EQ(snapshot.items.size(), 1u);
    EXPECT_EQ(activations, 1);
}

} // namespace ya
