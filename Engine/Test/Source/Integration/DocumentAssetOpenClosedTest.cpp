// A document asset that the engine has never heard of. Registration is the
// only hook: no AssetManager method, no resolver virtual, no editor enum.

#include "Core/Common/AssetDocumentManager.h"
#include "Core/Common/AssetTypeRegistry.h"
#include "Core/Common/DocumentAssetRef.h"
#include "Core/Log.h"
#include "Core/System/VirtualFileSystem.h"
#include "GameEditor/UI/Dialogs/EditorFilePicker.h"
#include "Resource/AssetManager.h"

#include <gtest/gtest.h>

#include <filesystem>
#include <memory>
#include <string>

namespace ya
{
namespace
{

struct ToyDocument
{
    int32_t value = 0;
};

struct ToyDocumentRef : DocumentAssetRef<ToyDocument>
{
    ToyDocumentRef() = default;
    explicit ToyDocumentRef(const std::string& path) : DocumentAssetRef<ToyDocument>(path) {}
};

struct ToyDocumentTraits
{
    static std::shared_ptr<ToyDocument> parse(const std::string& text, std::string& error)
    {
        if (text != "ok") {
            error = "bad toy";
            return nullptr;
        }
        auto document   = std::make_shared<ToyDocument>();
        document->value = 1;
        return document;
    }

    static void logCannotRead(const std::string& path)
    {
        YA_CORE_ERROR("ToyDocument: cannot read '{}'", path);
    }

    static void logInvalid(const std::string& path, const std::string& error)
    {
        YA_CORE_ERROR("ToyDocument: '{}' is not valid: {}", path, error);
    }
};

struct ToyDocumentRegistration
{
    ToyDocumentRegistration()
    {
        AssetTypeRegistry::registerDocument<ToyDocument, ToyDocumentTraits, ToyDocumentRef>(
            "ToyDocument", "Select Toy", {".toy.json"});
    }
};

const ToyDocumentRegistration g_toyDocumentRegistration;

class DocumentAssetOpenClosedTest : public ::testing::Test
{
  protected:
    void SetUp() override
    {
        if (!VirtualFileSystem::get()) {
            VirtualFileSystem::init();
        }
        AssetManager::get()->clearCache();
    }

    void TearDown() override
    {
        if (VirtualFileSystem* vfs = VirtualFileSystem::get()) {
            vfs->unmount("YaToyTest");
        }
        AssetManager::get()->clearCache();
    }
};

TEST_F(DocumentAssetOpenClosedTest, RegisteredRefSharesASlotAndRejectsABadDocument)
{
    auto* vfs = VirtualFileSystem::get();
    ASSERT_NE(vfs, nullptr);
    const auto dir = std::filesystem::temp_directory_path() / "ya-toy-document-test";
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir);
    vfs->unmount("YaToyTest");
    vfs->mount("YaToyTest", dir);
    vfs->saveToFile("YaToyTest:bad.toy.json", "nope");

    ToyDocumentRef first("YaToyTest:bad.toy.json");
    ToyDocumentRef second("YaToyTest:bad.toy.json");
    ASSERT_NE(first._handle, nullptr);
    EXPECT_EQ(first._handle, second._handle);
    EXPECT_EQ(first.getResolveState(), EAssetResolveState::Failed);
    EXPECT_FALSE(first.isLoaded());

    auto        document = std::make_shared<ToyDocument>();
    document->value      = 7;
    auto* store          = AssetTypeRegistry::get().store<ToyDocument>();
    ASSERT_NE(store, nullptr);
    store->registerAsset("toy-shared", document);
    ToyDocumentRef ready("toy-shared");
    ToyDocumentRef readyCopy("toy-shared");
    EXPECT_EQ(ready._handle, readyCopy._handle);
    EXPECT_EQ(ready.getResolveState(), EAssetResolveState::Ready);
    ASSERT_NE(ready.get(), nullptr);
    EXPECT_EQ(ready.get()->value, 7);

    std::filesystem::remove_all(dir);
}

TEST_F(DocumentAssetOpenClosedTest, AggregatesCoverTheRegisteredStore)
{
    auto* store = AssetTypeRegistry::get().store<ToyDocument>();
    ASSERT_NE(store, nullptr);

    auto document   = std::make_shared<ToyDocument>();
    document->value = 3;
    store->registerAsset("Content/Toys/held.toy.json", document);
    ToyDocumentRef held("Content/Toys/held.toy.json");
    ASSERT_EQ(held.getResolveState(), EAssetResolveState::Ready);

    AssetManager::get()->unload("Content/Toys/held.toy.json");
    EXPECT_EQ(held.getResolveState(), EAssetResolveState::Failed);
    EXPECT_FALSE(store->isLoaded("Content/Toys/held.toy.json"));

    store->registerAsset("Content/Toys/stale.toy.json", document);
    ToyDocumentRef stale("Content/Toys/stale.toy.json");
    const auto     staleSlot = stale._handle;
    AssetManager::get()->invalidate("Content/Toys/stale.toy.json");
    EXPECT_EQ(stale._handle, staleSlot);
    EXPECT_EQ(stale.getResolveState(), EAssetResolveState::Ready);
    ToyDocumentRef fresh("Content/Toys/stale.toy.json");
    EXPECT_NE(fresh._handle, staleSlot);
    EXPECT_EQ(fresh.getResolveState(), EAssetResolveState::Failed);

    {
        auto unused   = std::make_shared<ToyDocument>();
        unused->value = 1;
        store->registerAsset("toy-unused", unused);
    }
    EXPECT_TRUE(store->isLoaded("toy-unused"));
    AssetManager::get()->collectUnused();
    EXPECT_FALSE(store->isLoaded("toy-unused"));

    store->registerAsset("toy-cleared", document);
    ToyDocumentRef cleared("toy-cleared");
    const auto     clearedSlot = cleared._handle;
    AssetManager::get()->clearCache();
    EXPECT_EQ(cleared.getResolveState(), EAssetResolveState::Failed);
    EXPECT_FALSE(store->isLoaded("toy-cleared"));
    ToyDocumentRef afterClear("toy-cleared");
    EXPECT_NE(afterClear._handle, clearedSlot);
}

TEST_F(DocumentAssetOpenClosedTest, RegistryKnowsTheRefAndBuildsAPickerRequest)
{
    EXPECT_TRUE(isAssetRefType(ya::type_index_v<ToyDocumentRef>));
    const std::optional<AssetTypeDesc> desc = AssetTypeRegistry::get().findByPath("Content/Toys/hero.toy.json");
    ASSERT_TRUE(desc.has_value());
    EXPECT_EQ(desc->name, "ToyDocument");
    EXPECT_EQ(desc->displayName, "Select Toy");
    ASSERT_EQ(desc->extensions.size(), 1u);
    EXPECT_EQ(desc->extensions[0], ".toy.json");
    EXPECT_EQ(desc->refType, ya::type_index_v<ToyDocumentRef>);

    const FEditorFilePickerRequest request = makeAssetPickerRequest(*desc, "Content/Toys", {});
    EXPECT_EQ(request.title, "Select Toy");
    ASSERT_EQ(request.extensions.size(), 1u);
    EXPECT_EQ(request.extensions[0], ".toy.json");
    EXPECT_EQ(request.currentPath, "Content/Toys");
}

} // namespace
} // namespace ya
