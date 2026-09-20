// UIDocumentStore is the only thing that turns SceneWidgetEntry::documentPath
// into a live UIDocument. These guards cover the two halves a Scene depends on:
// the on-disk document form survives a round trip, and display lookups never
// trigger a file read.

#include "GUI/Widgets/UIDocumentStore.h"

#include "Core/System/VirtualFileSystem.h"
#include "GUI/Widgets/Controls/Border.h"
#include "GUI/Widgets/UITypeIds.h"
#include "GUI/Widgets/UITypeRegistry.h"

#include <filesystem>

#include <gtest/gtest.h>

namespace ya
{

namespace
{

constexpr std::string_view kDocumentPath = "Content/UI/HUD.yaui";

std::shared_ptr<UIDocument> makeDocument(float red)
{
    auto document    = std::make_shared<UIDocument>();
    document->typeId = kTypeIdBorder;
    document->fields = nlohmann::json{{"_color", {red, 0.0, 0.0, 1.0}}};
    return document;
}

glm::vec4 colorOf(const UIElement& widget)
{
    const auto* border = dynamic_cast<const UIBorder*>(&widget);
    return border ? border->getColor() : glm::vec4(-1.0f);
}

/// Owner of the temp root the store reads and writes through the VFS.
struct FTempDocumentRoot
{
    std::filesystem::path originalCwd = std::filesystem::current_path();
    std::filesystem::path root;

    explicit FTempDocumentRoot(std::string_view label)
    {
        root = originalCwd / std::filesystem::path("ya-ui-document-store-test-" + std::string(label));
        std::filesystem::remove_all(root);
        std::filesystem::create_directories(root);
        std::filesystem::current_path(root);
        VirtualFileSystem::init();
    }

    ~FTempDocumentRoot()
    {
        std::filesystem::current_path(originalCwd);
        std::filesystem::remove_all(root);
    }
};

} // namespace

TEST(UIDocumentStoreTest, SavedDocumentReloadsFromDisk)
{
    FTempDocumentRoot workspace("roundtrip");

    UIDocumentStore writer;
    writer.put(kDocumentPath, makeDocument(0.25f));
    ASSERT_TRUE(writer.save(kDocumentPath));
    EXPECT_TRUE(std::filesystem::exists(workspace.root / std::filesystem::path(kDocumentPath)));

    // A second store has no live document for the path, so it must read the
    // file the first one wrote.
    UIDocumentStore reader;
    EXPECT_EQ(reader.find(kDocumentPath), nullptr);
    const std::shared_ptr<UIDocument> reloaded = reader.resolve(kDocumentPath);
    ASSERT_NE(reloaded, nullptr);
    EXPECT_EQ(reloaded->typeId, kTypeIdBorder);

    // Instantiation is the consumer contract: the document still builds a
    // widget after crossing the file boundary.
    const UIElementRef widget = reloaded->instantiate();
    ASSERT_NE(widget, nullptr);
    EXPECT_EQ(widget->_typeId, kTypeIdBorder);
    EXPECT_EQ(colorOf(*widget), glm::vec4(0.25f, 0.0f, 0.0f, 1.0f));
}

TEST(UIDocumentStoreTest, SaveWritesTheCurrentLiveDocument)
{
    FTempDocumentRoot workspace("overwrite");

    UIDocumentStore store;
    store.put(kDocumentPath, makeDocument(0.25f));
    ASSERT_TRUE(store.save(kDocumentPath));

    // An authoring edit is a put(); the next save must persist the edit, not
    // the document that was on disk before it.
    store.put(kDocumentPath, makeDocument(0.75f));
    ASSERT_TRUE(store.save(kDocumentPath));

    UIDocumentStore reader;
    const std::shared_ptr<UIDocument> reloaded = reader.resolve(kDocumentPath);
    ASSERT_NE(reloaded, nullptr);
    ASSERT_NE(reloaded->instantiate(), nullptr);
    EXPECT_EQ(colorOf(*reloaded->instantiate()), glm::vec4(0.75f, 0.0f, 0.0f, 1.0f));
}

TEST(UIDocumentStoreTest, UnknownAndMalformedPathsResolveToNull)
{
    FTempDocumentRoot workspace("missing");

    UIDocumentStore store;
    EXPECT_EQ(store.resolve("Content/UI/Absent.yaui"), nullptr);

    const std::filesystem::path malformed = workspace.root / "Content" / "UI" / "Broken.yaui";
    std::filesystem::create_directories(malformed.parent_path());
    {
        std::ofstream file(malformed);
        file << "{ not json";
    }
    EXPECT_EQ(store.resolve("Content/UI/Broken.yaui"), nullptr);
}

TEST(UIDocumentStoreTest, SaveWithoutKnownDocumentFails)
{
    FTempDocumentRoot workspace("nosuch");

    UIDocumentStore store;
    EXPECT_FALSE(store.save(kDocumentPath));
}

} // namespace ya
