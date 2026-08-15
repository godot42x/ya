// Game UI hierarchy drag-drop reparenting tests: moveWidgetEntryDocument()
// (scene-core). Covers entry nesting/reorder, nested-node moves, cycle
// guards, non-inline targets, and the top-level position adjustment.

#include "Core/Reflection/DeferredInitializer.h"
#include "Scene/Core/SceneWidgetEntry.h"

#include <gtest/gtest.h>

#include <unordered_map>

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

std::shared_ptr<UIDocument> makeDoc(std::string typeId,
                                    nlohmann::json fields,
                                    std::vector<std::shared_ptr<UIDocument>> children = {})
{
    auto document  = std::make_shared<UIDocument>();
    document->typeId = std::move(typeId);
    document->fields = std::move(fields);
    document->children = std::move(children);
    return document;
}

/// Absolute-position fields (UIElement base block, canvas-relative).
nlohmann::json posFields(double x, double y, double w = 100.0, double h = 50.0)
{
    return nlohmann::json{
        {"__base__", nlohmann::json{{"UIElement",
                                     nlohmann::json{{"_position", {x, y}}, {"_size", {w, h}}}}}},
    };
}

SceneWidgetEntry makeEntry(std::string entryId, std::shared_ptr<UIDocument> document)
{
    SceneWidgetEntry entry;
    entry.entryId        = std::move(entryId);
    entry.inlineDocument = std::move(document);
    entry.autoMount      = true;
    return entry;
}

std::vector<SceneWidgetEntry> makeFlatHudEntries()
{
    // Mirrors the legacy Canvas > (Panel, Title, Label, Click Me) migration.
    return {
        makeEntry("Panel", makeDoc("engine.panel", posFields(20.0, 20.0, 300.0, 120.0))),
        makeEntry("Title", makeDoc("engine.text", posFields(36.0, 30.0))),
        makeEntry("Label", makeDoc("engine.text", posFields(36.0, 66.0))),
        makeEntry("Click Me", makeDoc("engine.button", posFields(36.0, 96.0))),
    };
}

} // namespace

// Nesting a top-level entry into another top-level entry (the core
// "flat -> nested" fix): the source entry disappears and its document becomes
// a child; the position converts from canvas-relative to parent-relative so
// the runtime layout does not shift.
TEST(SceneWidgetEntryReparentTest, NestTopLevelEntryIntoEntryAdjustsPosition)
{
    ensureReflectionReady();
    auto entries = makeFlatHudEntries();

    ASSERT_TRUE(moveWidgetEntryDocument(entries,
                                        /*srcEntryIndex=*/1, /*srcPath=*/{}, // Title
                                        /*dstEntryIndex=*/0, /*dstPath=*/{}, // Panel
                                        EWidgetEntryDropPosition::Into));

    ASSERT_EQ(entries.size(), 3u);
    ASSERT_EQ(entries[0].entryId, "Panel");
    ASSERT_EQ(entries[0].inlineDocument->children.size(), 1u);
    ASSERT_EQ(entries[0].inlineDocument->children[0]->typeId, "engine.text");

    // Title was at canvas (36,30); Panel is at (20,20) => parent-relative (16,10).
    auto title = entries[0].inlineDocument->children[0]->instantiate();
    ASSERT_NE(title, nullptr);
    EXPECT_NEAR(title->getPosition().x, 16.0, 1e-3);
    EXPECT_NEAR(title->getPosition().y, 10.0, 1e-3);

    // Remaining entries are Label and Click Me (in order).
    EXPECT_EQ(entries[1].entryId, "Label");
    EXPECT_EQ(entries[2].entryId, "Click Me");
}

// Nesting preserves the original position when the parent sits at the origin.
TEST(SceneWidgetEntryReparentTest, NestUnderOriginKeepsPosition)
{
    ensureReflectionReady();
    auto entries = makeFlatHudEntries();
    entries[0].inlineDocument->fields =
        posFields(0.0, 0.0, 300.0, 120.0); // Panel at origin

    ASSERT_TRUE(moveWidgetEntryDocument(entries, 1, {}, 0, {}, EWidgetEntryDropPosition::Into));
    auto title = entries[0].inlineDocument->children[0]->instantiate();
    ASSERT_NE(title, nullptr);
    EXPECT_NEAR(title->getPosition().x, 36.0, 1e-3);
    EXPECT_NEAR(title->getPosition().y, 30.0, 1e-3);
}

// Entry-level reorder (Before / After on entry rows).
TEST(SceneWidgetEntryReparentTest, ReorderEntries)
{
    ensureReflectionReady();
    auto entries = makeFlatHudEntries(); // Panel, Title, Label, Click Me

    // Click Me Before Panel.
    ASSERT_TRUE(moveWidgetEntryDocument(entries, 3, {}, 0, {}, EWidgetEntryDropPosition::Before));
    ASSERT_EQ(entries.size(), 4u);
    EXPECT_EQ(entries[0].entryId, "Click Me");
    EXPECT_EQ(entries[1].entryId, "Panel");
    EXPECT_EQ(entries[2].entryId, "Title");
    EXPECT_EQ(entries[3].entryId, "Label");

    // Panel After Label.
    ASSERT_TRUE(moveWidgetEntryDocument(entries, 1, {}, 3, {}, EWidgetEntryDropPosition::After));
    EXPECT_EQ(entries[0].entryId, "Click Me");
    EXPECT_EQ(entries[1].entryId, "Title");
    EXPECT_EQ(entries[2].entryId, "Label");
    EXPECT_EQ(entries[3].entryId, "Panel");
}

// A nested document node can be moved into another entry's root.
TEST(SceneWidgetEntryReparentTest, NestedNodeIntoAnotherEntryRoot)
{
    ensureReflectionReady();
    std::vector<SceneWidgetEntry> entries = {
        makeEntry("Menu", makeDoc("engine.container", {}, {
            makeDoc("engine.text", posFields(0.0, 0.0)),  // Title (child)
            makeDoc("engine.panel", posFields(0.0, 40.0)), // Panel (child)
        })),
        makeEntry("Overlay", makeDoc("engine.panel", posFields(0.0, 0.0))),
    };

    // Move Menu's Title child into Overlay (append as Overlay's child).
    ASSERT_TRUE(moveWidgetEntryDocument(entries, 0, {0}, 1, {}, EWidgetEntryDropPosition::Into));

    ASSERT_EQ(entries.size(), 2u);
    ASSERT_EQ(entries[0].inlineDocument->children.size(), 1u);
    EXPECT_EQ(entries[0].inlineDocument->children[0]->typeId, "engine.panel");
    ASSERT_EQ(entries[1].inlineDocument->children.size(), 1u);
    EXPECT_EQ(entries[1].inlineDocument->children[0]->typeId, "engine.text");
}

// Sibling reorder inside one document (Before / After on document nodes).
TEST(SceneWidgetEntryReparentTest, NestedSiblingReorder)
{
    ensureReflectionReady();
    std::vector<SceneWidgetEntry> entries = {
        makeEntry("Menu", makeDoc("engine.container", {}, {
            makeDoc("engine.text", posFields(0.0, 0.0)),   // [0]
            makeDoc("engine.text", posFields(0.0, 30.0)),  // [1]
            makeDoc("engine.text", posFields(0.0, 60.0)),  // [2]
        })),
    };

    // Move [2] Before [0].
    ASSERT_TRUE(moveWidgetEntryDocument(entries, 0, {2}, 0, {0}, EWidgetEntryDropPosition::Before));
    ASSERT_EQ(entries[0].inlineDocument->children.size(), 3u);
    EXPECT_EQ(entries[0].inlineDocument->children[0]->fields["__base__"]["UIElement"]["_position"][1], 60.0);
    EXPECT_EQ(entries[0].inlineDocument->children[1]->fields["__base__"]["UIElement"]["_position"][1], 0.0);
    EXPECT_EQ(entries[0].inlineDocument->children[2]->fields["__base__"]["UIElement"]["_position"][1], 30.0);
}

// Dropping an entry into its own subtree must be rejected (cycle).
TEST(SceneWidgetEntryReparentTest, RejectsCycle)
{
    ensureReflectionReady();
    std::vector<SceneWidgetEntry> entries = {
        makeEntry("Panel", makeDoc("engine.panel", {}, {makeDoc("engine.text", posFields(0.0, 0.0))})),
    };

    // Panel Into its own child Title.
    EXPECT_FALSE(moveWidgetEntryDocument(entries, 0, {}, 0, {0}, EWidgetEntryDropPosition::Into));
    ASSERT_EQ(entries.size(), 1u);
    ASSERT_EQ(entries[0].inlineDocument->children.size(), 1u);
}

// documentPath entries cannot receive children (no inline container).
TEST(SceneWidgetEntryReparentTest, RejectsDocumentPathTarget)
{
    ensureReflectionReady();
    std::vector<SceneWidgetEntry> entries = {
        makeEntry("A", makeDoc("engine.panel", posFields(0.0, 0.0))),
        SceneWidgetEntry{.entryId = "B", .documentPath = "Content/UI/B.yaui"},
    };

    EXPECT_FALSE(moveWidgetEntryDocument(entries, 0, {}, 1, {}, EWidgetEntryDropPosition::Into));
    EXPECT_EQ(entries.size(), 2u);
}

// A nested widget cannot become a top-level entry via Before/After.
TEST(SceneWidgetEntryReparentTest, RejectsNestedBeforeEntryRoot)
{
    ensureReflectionReady();
    std::vector<SceneWidgetEntry> entries = {
        makeEntry("Menu", makeDoc("engine.container", {}, {makeDoc("engine.text", posFields(0.0, 0.0))})),
        makeEntry("Overlay", makeDoc("engine.panel", posFields(0.0, 0.0))),
    };

    EXPECT_FALSE(moveWidgetEntryDocument(entries, 0, {0}, 1, {}, EWidgetEntryDropPosition::Before));
    ASSERT_EQ(entries[0].inlineDocument->children.size(), 1u);
}

// The exact authoring workflow the flat-vs-nested complaint describes: the
// legacy scene migrates Canvas children into flat entries; dragging each
// sibling into the Panel rebuilds the nesting with preserved positions.
TEST(SceneWidgetEntryReparentTest, FlattenedHudRebuildsNestingByDragDrop)
{
    ensureReflectionReady();
    auto entries = makeFlatHudEntries(); // Panel, Title, Label, Click Me

    // After each drag the entry vector shrinks, so indices shift: the next
    // sibling is always at index 1 (the panel stays at index 0).
    ASSERT_TRUE(moveWidgetEntryDocument(entries, 1, {}, 0, {}, EWidgetEntryDropPosition::Into)); // Title
    ASSERT_TRUE(moveWidgetEntryDocument(entries, 1, {}, 0, {}, EWidgetEntryDropPosition::Into)); // Label
    ASSERT_TRUE(moveWidgetEntryDocument(entries, 1, {}, 0, {}, EWidgetEntryDropPosition::Into)); // Click Me

    ASSERT_EQ(entries.size(), 1u);
    ASSERT_EQ(entries[0].entryId, "Panel");
    ASSERT_EQ(entries[0].inlineDocument->children.size(), 3u);

    // Positions stay visually identical (panel origin 20,20 subtracted).
    const double expected[3][2] = {{16.0, 10.0}, {16.0, 46.0}, {16.0, 76.0}};
    for (size_t i = 0; i < 3; ++i) {
        auto widget = entries[0].inlineDocument->children[i]->instantiate();
        ASSERT_NE(widget, nullptr);
        EXPECT_NEAR(widget->getPosition().x, expected[i][0], 1e-3);
        EXPECT_NEAR(widget->getPosition().y, expected[i][1], 1e-3);
    }
    EXPECT_EQ(entries[0].inlineDocument->children[0]->typeId, "engine.text");
    EXPECT_EQ(entries[0].inlineDocument->children[1]->typeId, "engine.text");
    EXPECT_EQ(entries[0].inlineDocument->children[2]->typeId, "engine.button");
}

// File-backed (documentPath) entries participate through the resolve callback:
// the destination .yaui gains the moved document and is reported for persist.
TEST(SceneWidgetEntryReparentTest, FileBackedTargetAcceptsChildAndReportsChangedFile)
{
    ensureReflectionReady();
    auto panelDoc = makeDoc("engine.panel", posFields(20.0, 20.0, 300.0, 120.0));
    auto overlayDoc = makeDoc("engine.panel", posFields(0.0, 0.0));

    std::vector<SceneWidgetEntry> entries = {
        makeEntry("Panel", panelDoc),
        SceneWidgetEntry{.entryId = "Overlay", .documentPath = "Content/UI/Overlay.yaui"},
    };
    // The resolver map stands in for the host UIDocumentResolver cache.
    std::unordered_map<std::string, std::shared_ptr<UIDocument>> files{{"Content/UI/Overlay.yaui", overlayDoc}};
    const auto resolve = [&](const std::string& path) -> std::shared_ptr<UIDocument> {
        const auto it = files.find(path);
        return it == files.end() ? nullptr : it->second;
    };
    std::vector<std::string> changedFiles;

    // Drag the inline Panel entry Into the file-backed Overlay entry.
    ASSERT_TRUE(moveWidgetEntryDocument(entries, 0, {}, 1, {}, EWidgetEntryDropPosition::Into,
                                        resolve, &changedFiles));

    // The Panel entry is gone; Overlay's document gained the Panel child.
    ASSERT_EQ(entries.size(), 1u);
    EXPECT_EQ(entries[0].documentPath, "Content/UI/Overlay.yaui");
    ASSERT_EQ(overlayDoc->children.size(), 1u);
    EXPECT_EQ(overlayDoc->children[0]->typeId, "engine.panel");

    ASSERT_EQ(changedFiles.size(), 1u);
    EXPECT_EQ(changedFiles[0], "Content/UI/Overlay.yaui");
}

// Moving a nested node OUT of a file-backed entry reports that file too.
TEST(SceneWidgetEntryReparentTest, FileBackedSourceNodeRemovalReportsChangedFile)
{
    ensureReflectionReady();
    auto menuDoc = makeDoc("engine.container", {}, {makeDoc("engine.text", posFields(0.0, 0.0))});
    auto overlayDoc = makeDoc("engine.panel", posFields(0.0, 0.0));

    std::vector<SceneWidgetEntry> entries = {
        SceneWidgetEntry{.entryId = "Menu", .documentPath = "Content/UI/Menu.yaui"},
        makeEntry("Overlay", overlayDoc),
    };
    std::unordered_map<std::string, std::shared_ptr<UIDocument>> files{{"Content/UI/Menu.yaui", menuDoc}};
    const auto resolve = [&](const std::string& path) -> std::shared_ptr<UIDocument> {
        const auto it = files.find(path);
        return it == files.end() ? nullptr : it->second;
    };
    std::vector<std::string> changedFiles;

    // Move Menu's text child (src path [0]) Into the Overlay entry.
    ASSERT_TRUE(moveWidgetEntryDocument(entries, 0, {0}, 1, {}, EWidgetEntryDropPosition::Into,
                                        resolve, &changedFiles));

    EXPECT_TRUE(menuDoc->children.empty());
    ASSERT_EQ(overlayDoc->children.size(), 1u);
    EXPECT_EQ(overlayDoc->children[0]->typeId, "engine.text");
    ASSERT_EQ(changedFiles.size(), 1u);
    EXPECT_EQ(changedFiles[0], "Content/UI/Menu.yaui");
}

// A file entry whose document cannot be resolved is rejected (not silent).
TEST(SceneWidgetEntryReparentTest, FileBackedUnresolvableRejected)
{
    ensureReflectionReady();
    std::vector<SceneWidgetEntry> entries = {
        makeEntry("Panel", makeDoc("engine.panel", posFields(0.0, 0.0))),
        SceneWidgetEntry{.entryId = "Missing", .documentPath = "Content/UI/Missing.yaui"},
    };
    const auto resolve = [](const std::string&) -> std::shared_ptr<UIDocument> { return nullptr; };

    EXPECT_FALSE(moveWidgetEntryDocument(entries, 0, {}, 1, {}, EWidgetEntryDropPosition::Into,
                                         resolve, nullptr));
    ASSERT_EQ(entries.size(), 2u);
}

// Validation-only preview: valid moves report true WITHOUT mutating entries.
TEST(SceneWidgetEntryReparentTest, CanMoveReportsValidWithoutMutating)
{
    ensureReflectionReady();
    auto entries = makeFlatHudEntries(); // Panel, Title, Label, Click Me

    EXPECT_TRUE(canMoveWidgetEntryDocument(entries, 1, {}, 0, {}, EWidgetEntryDropPosition::Into));
    EXPECT_TRUE(canMoveWidgetEntryDocument(entries, 3, {}, 1, {}, EWidgetEntryDropPosition::Before));
    ASSERT_EQ(entries.size(), 4u); // untouched
    ASSERT_EQ(entries[0].inlineDocument->children.size(), 0u);
}

// Self-drops and cycles are invalid (red feedback in the editor).
TEST(SceneWidgetEntryReparentTest, CanMoveRejectsSelfAndCycle)
{
    ensureReflectionReady();
    std::vector<SceneWidgetEntry> entries = {
        makeEntry("Panel", makeDoc("engine.panel", posFields(0.0, 0.0), {
            makeDoc("engine.text", posFields(0.0, 0.0)),  // child [0]
        })),
        makeEntry("Overlay", makeDoc("engine.panel", posFields(0.0, 0.0))),
    };

    // Self-drop (entry onto itself).
    EXPECT_FALSE(canMoveWidgetEntryDocument(entries, 0, {}, 0, {}, EWidgetEntryDropPosition::Into));
    // Cycle: Panel Into its own child.
    EXPECT_FALSE(canMoveWidgetEntryDocument(entries, 0, {}, 0, {0}, EWidgetEntryDropPosition::Into));
    // Unresolvable target (documentPath without a resolver).
    entries.push_back(SceneWidgetEntry{.entryId = "Missing", .documentPath = "Content/UI/Missing.yaui"});
    EXPECT_FALSE(canMoveWidgetEntryDocument(entries, 0, {}, 2, {}, EWidgetEntryDropPosition::Into));
    // Nested source cannot become a top-level entry via Before/After.
    EXPECT_FALSE(canMoveWidgetEntryDocument(entries, 0, {0}, 1, {}, EWidgetEntryDropPosition::Before));
    // A valid move still reports true afterwards.
    EXPECT_TRUE(canMoveWidgetEntryDocument(entries, 0, {0}, 1, {}, EWidgetEntryDropPosition::Into));
    // Nothing mutated by any of the checks above.
    ASSERT_EQ(entries[0].inlineDocument->children.size(), 1u);
    ASSERT_EQ(entries[1].inlineDocument->children.size(), 0u);
}

// Dropping an entry onto itself is a no-op.
TEST(SceneWidgetEntryReparentTest, NoOpOnSelf)
{
    ensureReflectionReady();
    auto entries = makeFlatHudEntries();

    EXPECT_TRUE(moveWidgetEntryDocument(entries, 2, {}, 2, {}, EWidgetEntryDropPosition::Into));
    EXPECT_TRUE(moveWidgetEntryDocument(entries, 2, {}, 2, {}, EWidgetEntryDropPosition::Before));
    ASSERT_EQ(entries.size(), 4u);
}

} // namespace ya
