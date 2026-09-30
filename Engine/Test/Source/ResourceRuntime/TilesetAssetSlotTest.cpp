// Tileset-slot regression guards (resource-handle-events H3). Tileset slots
// fill synchronously (authoring JSON parsed on first request), so a ref
// binds Ready or Failed immediately; same-path refs and copies share the
// slot. No render backend and no task queue are involved.

#include "Core/Common/Tileset.h"
#include "Core/System/VirtualFileSystem.h"
#include "Resource/AssetManager.h"

#include <gtest/gtest.h>

namespace ya
{
namespace
{

constexpr const char* kMissingTileset = "Content/Tilesets/__tileset_slot_missing.yatileset.json";
constexpr const char* kTilesetName    = "__tileset_slot_registered";

class TilesetAssetSlotTest : public ::testing::Test
{
  protected:
    void SetUp() override
    {
        // Tileset documents parse through the VFS, which hosts mount first.
        if (!VirtualFileSystem::get()) {
            VirtualFileSystem::init();
        }
        // Drop entries any earlier suite may have left in the shared
        // AssetManager.
        AssetManager::get()->clearCache();
    }

    void TearDown() override
    {
        AssetManager::get()->clearCache();
    }
};

TEST_F(TilesetAssetSlotTest, MissingFileFailsEverySharingRefImmediately)
{
    TilesetRef first(kMissingTileset);
    TilesetRef copy = first;
    TilesetRef second;
    second.setPath(kMissingTileset);

    ASSERT_NE(first._handle, nullptr);
    EXPECT_EQ(first._handle, copy._handle);
    EXPECT_EQ(first._handle, second._handle);
    EXPECT_EQ(first.getResolveState(), EAssetResolveState::Failed);
    EXPECT_EQ(first.isLoaded(), false);
    EXPECT_EQ(first.get(), nullptr);
    EXPECT_EQ(first._handle->generation, 1u);
    EXPECT_FALSE(AssetManager::get()->isTilesetLoaded(kMissingTileset));
    EXPECT_EQ(AssetManager::get()->getTileset(kMissingTileset), nullptr);
}

TEST_F(TilesetAssetSlotTest, RegisteredTilesetIsOneReadySlotUntilUnused)
{
    auto       tileset  = std::make_shared<Tileset>();
    TilesetRef first(kTilesetName);
    TilesetRef copy = first;
    auto       slot = first._handle;

    AssetManager::get()->registerTileset(kTilesetName, tileset);

    // Rebinding after the registration binds the same Ready slot.
    TilesetRef second;
    second.setPath(kTilesetName);

    EXPECT_EQ(first.getResolveState(), EAssetResolveState::Ready);
    EXPECT_EQ(first._handle, copy._handle);
    EXPECT_EQ(first._handle, second._handle);
    EXPECT_EQ(first._handle, slot);
    EXPECT_EQ(first.get(), tileset.get());
    EXPECT_TRUE(AssetManager::get()->isTilesetLoaded(kTilesetName));
    EXPECT_EQ(slot->generation, 2u);

    // The refs drop their slots; only the manager still holds it.
    TilesetRef keeper = first;
    first   = TilesetRef{};
    copy   = TilesetRef{};
    second = TilesetRef{};
    tileset = nullptr;
    slot   = nullptr;
    EXPECT_EQ(AssetManager::get()->collectUnused(), 0u);
    EXPECT_TRUE(AssetManager::get()->isTilesetLoaded(kTilesetName));

    keeper = TilesetRef{};
    EXPECT_EQ(AssetManager::get()->collectUnused(), 1u);
    EXPECT_FALSE(AssetManager::get()->isTilesetLoaded(kTilesetName));
}

TEST_F(TilesetAssetSlotTest, InvalidateDropsTheEntryAndNextAcquireParsesAgain)
{
    auto       tileset = std::make_shared<Tileset>();
    TilesetRef ref(kTilesetName);
    const auto slot = ref._handle;

    AssetManager::get()->registerTileset(kTilesetName, tileset);
    EXPECT_EQ(ref.getResolveState(), EAssetResolveState::Ready);

    AssetManager::get()->invalidate(kTilesetName);
    // Holders keep the old slot (still Ready) until they rebind; a fresh
    // ref parses the document again into a NEW slot.
    EXPECT_EQ(ref.getResolveState(), EAssetResolveState::Ready);
    EXPECT_EQ(ref._handle, slot);

    TilesetRef fresh(kTilesetName);
    EXPECT_NE(fresh._handle, slot);
    EXPECT_EQ(fresh.getResolveState(), EAssetResolveState::Failed);
}

TEST_F(TilesetAssetSlotTest, UnloadFailsHeldSlotsAndNotifiesObservers)
{
    auto       tileset = std::make_shared<Tileset>();
    TilesetRef ref(kTilesetName);
    AssetManager::get()->registerTileset(kTilesetName, tileset);

    int        notifications = 0;
    const auto before       = ref._handle->generation;
    auto       token        = ref._handle->observers.subscribe([&] { ++notifications; });

    AssetManager::get()->unload(kTilesetName);
    EXPECT_EQ(ref.getResolveState(), EAssetResolveState::Failed);
    EXPECT_EQ(ref.get(), nullptr);
    EXPECT_EQ(ref._handle->generation, before + 1);
    EXPECT_EQ(notifications, 1);
}

TEST_F(TilesetAssetSlotTest, EmptyPathBindsNothing)
{
    TilesetRef ref(kMissingTileset);
    ref.setPath("");
    EXPECT_EQ(ref._handle, nullptr);
    EXPECT_EQ(ref.getResolveState(), EAssetResolveState::Empty);
}

} // namespace
} // namespace ya
