// Game UI hierarchy drag-drop reparenting tests: moveWidgetEntryDocument()
// (scene-core). Covers entry nesting/reorder, nested-node moves, cycle
// guards, non-inline targets, and parent-owned root-slot transfer.

#include "Core/Reflection/DeferredInitializer.h"
#include "Scene/Core/SceneWidgetEntry.h"

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

std::shared_ptr<UIDocument> makeDoc(std::string typeId,
                                    nlohmann::json fields,
                                    std::vector<std::shared_ptr<UIDocument>> children = {},
                                    std::vector<nlohmann::json> childSlots = {})
{
    auto document  = std::make_shared<UIDocument>();
    document->typeId = std::move(typeId);
    document->fields = std::move(fields);
    document->children = std::move(children);
    document->childSlots = std::move(childSlots);
    return document;
}

nlohmann::json posFields(double x, double y, double w = 100.0, double h = 50.0)
{
    return nlohmann::json{{"__base__", nlohmann::json{{"UIElement",
        nlohmann::json{{"_position", {x, y}}, {"_size", {w, h}}}}}}};
}

SceneWidgetEntry makeEntry(std::string entryId, std::shared_ptr<UIDocument> document,
                           glm::vec2 offset = {}, glm::vec2 fixedSize = {})
{
    SceneWidgetEntry entry;
    entry.entryId        = std::move(entryId);
    entry.inlineDocument = std::move(document);
    entry.rootSlot.offset = offset;
    entry.rootSlot.fixedSize = fixedSize;
    entry.autoMount      = true;
    return entry;
}

std::vector<SceneWidgetEntry> makeFlatHudEntries()
{
    // New authoring data stores geometry on the scene->entry edge.
    return {
        makeEntry("Panel", makeDoc("engine.panel", {}), {20.0f, 20.0f}, {300.0f, 120.0f}),
        makeEntry("Title", makeDoc("engine.text", {}), {36.0f, 30.0f}, {260.0f, 26.0f}),
        makeEntry("Label", makeDoc("engine.text", {}), {36.0f, 66.0f}, {260.0f, 20.0f}),
        makeEntry("Click Me", makeDoc("engine.button", {}), {36.0f, 96.0f}, {140.0f, 30.0f}),
    };
}

} // namespace

// Nesting a top-level entry into another top-level entry transfers the source
// root edge intent to the destination document's child edge.
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

    ASSERT_EQ(entries[0].inlineDocument->childSlots.size(), 1u);
    EXPECT_EQ(entries[0].inlineDocument->childSlots[0]["offset"], nlohmann::json({36.0, 30.0}));

    // Remaining entries are Label and Click Me (in order).
    EXPECT_EQ(entries[1].entryId, "Label");
    EXPECT_EQ(entries[2].entryId, "Click Me");
}

TEST(SceneWidgetEntryReparentTest, NestTopLevelEntryCarriesRootSlotIntoChildEdge)
{
    ensureReflectionReady();
    auto entries = makeFlatHudEntries();
    ASSERT_TRUE(moveWidgetEntryDocument(entries, 1, {}, 0, {}, EWidgetEntryDropPosition::Into));
    ASSERT_EQ(entries[0].inlineDocument->childSlots.size(), 1u);
    EXPECT_EQ(entries[0].inlineDocument->childSlots[0]["offset"], nlohmann::json({36.0f, 30.0f}));
    EXPECT_EQ(entries[0].inlineDocument->childSlots[0]["fixedSize"], nlohmann::json({260.0f, 26.0f}));
}

// Nesting under an origin parent preserves the source root edge intent.
TEST(SceneWidgetEntryReparentTest, NestUnderOriginKeepsPosition)
{
    ensureReflectionReady();
    auto entries = makeFlatHudEntries();
    entries[0].rootSlot.offset = {0.0f, 0.0f}; // Panel at origin

    ASSERT_TRUE(moveWidgetEntryDocument(entries, 1, {}, 0, {}, EWidgetEntryDropPosition::Into));
    ASSERT_EQ(entries[0].inlineDocument->childSlots.size(), 1u);
    EXPECT_EQ(entries[0].inlineDocument->childSlots[0]["offset"], nlohmann::json({36.0, 30.0}));
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

TEST(SceneWidgetEntryReparentTest, NestedReparentMovesSlotIntentWithChild)
{
    ensureReflectionReady();
    const nlohmann::json firstSlot = {{"type", "canvas"}, {"offset", {11.0, 22.0}}};
    const nlohmann::json secondSlot = {{"type", "canvas"}, {"offset", {33.0, 44.0}}};
    auto menu = makeDoc("engine.panel", {},
                        {makeDoc("engine.text", {}), makeDoc("engine.text", {})},
                        {firstSlot, secondSlot});
    auto overlay = makeDoc("engine.panel", {});
    std::vector<SceneWidgetEntry> entries = {makeEntry("Menu", menu), makeEntry("Overlay", overlay)};

    ASSERT_TRUE(moveWidgetEntryDocument(entries, 0, {1}, 1, {}, EWidgetEntryDropPosition::Into));
    ASSERT_EQ(entries[0].inlineDocument->children.size(), 1u);
    ASSERT_EQ(entries[0].inlineDocument->childSlots.size(), 1u);
    EXPECT_EQ(entries[0].inlineDocument->childSlots[0], firstSlot);
    ASSERT_EQ(entries[1].inlineDocument->children.size(), 1u);
    ASSERT_EQ(entries[1].inlineDocument->childSlots.size(), 1u);
    EXPECT_EQ(entries[1].inlineDocument->childSlots[0], secondSlot);
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

// Entries without an inline document cannot receive children (nothing to
// attach into).
TEST(SceneWidgetEntryReparentTest, RejectsEntryWithoutInlineDocument)
{
    ensureReflectionReady();
    std::vector<SceneWidgetEntry> entries = {
        makeEntry("A", makeDoc("engine.panel", posFields(0.0, 0.0))),
        SceneWidgetEntry{.entryId = "B"},
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

    // Parent-owned edge intent stays visually identical; no widget geometry
    // fields are rewritten during the structural move.
    const double expected[3][2] = {{36.0, 30.0}, {36.0, 66.0}, {36.0, 96.0}};
    for (size_t i = 0; i < 3; ++i) {
        ASSERT_TRUE(entries[0].inlineDocument->childSlots[i].contains("offset"));
        EXPECT_NEAR(entries[0].inlineDocument->childSlots[i]["offset"][0].get<double>(), expected[i][0], 1e-3);
        EXPECT_NEAR(entries[0].inlineDocument->childSlots[i]["offset"][1].get<double>(), expected[i][1], 1e-3);
    }
    EXPECT_EQ(entries[0].inlineDocument->children[0]->typeId, "engine.text");
    EXPECT_EQ(entries[0].inlineDocument->children[1]->typeId, "engine.text");
    EXPECT_EQ(entries[0].inlineDocument->children[2]->typeId, "engine.button");
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
    // Unresolvable target (entry without an inline document).
    entries.push_back(SceneWidgetEntry{.entryId = "Missing"});
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
