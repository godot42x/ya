// Behaviour of the dock layout reader: the shipped document, this machine's
// arrangement, and which of the two a caller gets. The shipped files
// themselves are asserted to parse, because a broken one silently degrades
// every editor workspace to "no layout" -- which no other test would catch.

#include "GameEditor/UI/Dock/EditorLayoutLibrary.h"

#include "GameEditor/UI/Dock/EditorDockWorkspace.h"
#include "GameEditor/UI/Dock/EditorWindowLayout.h"
#include "GameEditor/UI/Shell/EditorWindowRegistry.h"

#include "GUI/Widgets/Controls/DockSpace/DockContext.h"

#include <filesystem>
#include <fstream>
#include <gtest/gtest.h>

namespace ya
{

namespace
{

/// A scratch workspace so the library's two roots resolve inside it. The roots
/// are ABSOLUTE on purpose: `translatePath` short-circuits an absolute path, so
/// this fixture never has to install a VFS root -- and so it cannot leak one
/// into the suites that run after it.
struct FTempLayoutRoot
{
    std::filesystem::path originalCwd = std::filesystem::current_path();
    std::filesystem::path root;

    explicit FTempLayoutRoot(std::string_view label)
    {
        root = originalCwd / std::filesystem::path("ya-editor-layout-test-" + std::string(label));
        std::filesystem::remove_all(root);
        std::filesystem::create_directories(root);
    }

    ~FTempLayoutRoot()
    {
        std::filesystem::remove_all(root);
    }

    [[nodiscard]] std::string defaultsRoot() const { return (root / "defaults").string(); }
    [[nodiscard]] std::string overridesRoot() const { return (root / "overrides").string(); }

    void write(std::string_view relative, const nlohmann::json& document) const
    {
        const std::filesystem::path path = root / std::filesystem::path(relative);
        std::filesystem::create_directories(path.parent_path());
        std::ofstream(path) << document.dump(2);
    }
};

} // namespace

TEST(EditorLayoutLibraryTest, LocalArrangementWinsOverTheShippedDocument)
{
    FTempLayoutRoot workspace("precedence");
    workspace.write("defaults/Level.json", nlohmann::json{{"shipped", true}});
    workspace.write("overrides/Level.json", nlohmann::json{{"arranged", true}});

    EditorLayoutLibrary library(FEditorLayoutRoots{.defaults  = workspace.defaultsRoot(),
                                                   .overrides = workspace.overridesRoot()});
    const nlohmann::json level = library.document("Level");
    ASSERT_TRUE(level.is_object());
    EXPECT_TRUE(level.value("arranged", false));
    EXPECT_FALSE(level.contains("shipped"));

    // No local arrangement yet: the shipped document is what a first run gets.
    const nlohmann::json windowRoot = library.document("WindowRoot");
    EXPECT_TRUE(windowRoot.empty());

    // Neither root has it: an empty document, never null. Callers ask it for
    // keys, and a null document throws there instead of falling back.
    EXPECT_TRUE(library.document("Missing").is_object());
    EXPECT_TRUE(library.document("Missing").empty());
}

TEST(EditorLayoutLibraryTest, SavedArrangementIsReadBackByTheNextInstance)
{
    FTempLayoutRoot workspace("roundtrip");
    EditorLayoutLibrary library(FEditorLayoutRoots{.defaults  = workspace.defaultsRoot(),
                                                   .overrides = workspace.overridesRoot()});

    const nlohmann::json arranged = {
        {"version", 2},
        {"tree", {{"kind", "leaf"}, {"id", "main"}}},
        {"dockSpace", {{"main", {{"panels", nlohmann::json::array({"viewport"})}}}}},
    };
    ASSERT_TRUE(library.saveOverride("UI", arranged));
    EXPECT_EQ(library.document("UI"), arranged);

    // The write lands under the overrides root, not the shipped one: a
    // developer's arrangement must not edit content under version control.
    EXPECT_TRUE(std::filesystem::exists(workspace.root / "overrides" / "UI.json"));
    EXPECT_FALSE(std::filesystem::exists(workspace.root / "defaults" / "UI.json"));

    // A malformed override is ignored rather than thrown: the page falls back
    // to the shipped document, which is what it does when the file is absent.
    // Written as text on purpose: it has to be malformed on disk, which is the
    // only way to exercise the parse guard.
    {
        std::ofstream(workspace.root / "overrides" / "UI.json") << "{ not json";
    }
    EXPECT_TRUE(library.document("UI").empty());
}

TEST(EditorLayoutLibraryTest, ShippedLayoutsParseAndNameThePanelsTheyPlace)
{
    // The shipped documents are content now, so nothing type-checks them. This
    // is the one gate that a renamed or malformed file cannot slip past: each
    // must be a valid dock document that places at least one panel.
    const nlohmann::json windowRoot = EditorDockWorkspace::factoryLayout();
    ASSERT_TRUE(windowRoot.is_object()) << "Engine/Config/Layout/Editor/WindowRoot.json";
    EXPECT_FALSE(FDockContext::collectLayoutPanelKeys(windowRoot).empty());

    const nlohmann::json level = EditorDockWorkspace::factoryOwnedNestedLayout();
    ASSERT_TRUE(level.is_object()) << "Engine/Config/Layout/Editor/Level.json";
    EXPECT_FALSE(FDockContext::collectLayoutPanelKeys(level).empty());

    for (const EditorRootId rootId : {kUIEditorRootId, kMaterialEditorRootId, kScriptEditorRootId}) {
        const nlohmann::json nested = EditorDockWorkspace::factoryOwnedNestedLayoutFor(rootId);
        ASSERT_TRUE(nested.is_object()) << EditorDockWorkspace::nestedLayoutDocumentName(rootId);
        EXPECT_FALSE(FDockContext::collectLayoutPanelKeys(nested).empty())
            << EditorDockWorkspace::nestedLayoutDocumentName(rootId);
    }
}

TEST(EditorLayoutLibraryTest, AnUnbuiltWindowWritesNoArrangement)
{
    FTempLayoutRoot workspace("unbuilt");

    // The persist entry point reads the process-wide binding, so this is the
    // one case that has to install its own roots. The previous pair is put back
    // before returning: a test that leaves the process pointed at a directory
    // it then deletes would break every suite that runs after it.
    const FEditorLayoutRoots previous = EditorLayoutLibrary::get().roots();
    EditorLayoutLibrary::get().bindRoots(FEditorLayoutRoots{.defaults  = workspace.defaultsRoot(),
                                                            .overrides = workspace.overridesRoot()});
    struct FRestoreRoots
    {
        FEditorLayoutRoots roots;
        ~FRestoreRoots() { EditorLayoutLibrary::get().bindRoots(roots); }
    } restore{previous};

    // A window whose chrome was never built exports a record with no dock
    // documents. Persisting that would replace a good arrangement with an empty
    // one -- and on a first run would freeze the shipped default before the
    // user arranged anything, so a later default improvement could never land.
    EditorWindowRegistry windows;
    persistEditorWindowLayout(windows, nullptr, nullptr);
    EXPECT_FALSE(std::filesystem::exists(workspace.root / "overrides" / "Workspace.json"));
    EXPECT_TRUE(EditorLayoutLibrary::get().document(kEditorLayoutWorkspace).empty());
}

} // namespace ya
