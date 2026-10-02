// Model-slot regression guards (resource-handle-events H3). loadModel hands
// out one shared AssetHandle<Model> slot per path; copies and later requests
// for the same path share it. These cases ride the missing-file failure path
// (decoding fails before any GPU upload), so no render backend is needed.

#include "Core/Async/TaskQueue.h"
#include "Core/Common/AssetRef.h"
#include "Core/System/VirtualFileSystem.h"
#include "Resource/AssetManager.h"

#include <gtest/gtest.h>

#include <chrono>
#include <thread>

namespace ya
{
namespace
{

// Paths that never exist, so every async decode fails on the worker.
constexpr const char* kMissingModelA = "Content/Models/__model_slot_missing_a.obj";
constexpr const char* kMissingModelB = "Content/Models/__model_slot_missing_b.obj";

class ModelAssetSlotTest : public ::testing::Test
{
  protected:
    void SetUp() override
    {
        // Loading resolves paths through the VFS, which hosts mount first.
        if (!VirtualFileSystem::get()) {
            VirtualFileSystem::init();
        }
        // Completion callbacks run inline; an App from an earlier test may
        // have left its frame sink installed. clearCache drops entries any
        // earlier suite may have left in the shared AssetManager.
        AssetManager::setFrameTaskSink({});
        AssetManager::get()->clearCache();
        TaskQueue::get().start(1);
    }

    void TearDown() override
    {
        AssetManager::get()->clearCache();
    }

    static bool pumpUntilSettled(const AssetHandle<Model>& handle)
    {
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
        while (handle->state == EAssetSlotState::Loading) {
            if (std::chrono::steady_clock::now() > deadline) {
                return false;
            }
            TaskQueue::get().processMainThreadCallbacks();
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        return true;
    }
};

TEST_F(ModelAssetSlotTest, SamePathSharesOneSlotAndCopiesShareIt)
{
    ModelRef first(kMissingModelA);
    ModelRef second;
    second.setPath(kMissingModelA);
    ModelRef copy = first;
    ModelRef other(kMissingModelB);

    ASSERT_NE(first._handle, nullptr);
    EXPECT_EQ(first._handle, second._handle);
    EXPECT_EQ(first._handle, copy._handle);
    EXPECT_NE(first._handle, other._handle);
    EXPECT_EQ(first.getResolveState(), EAssetResolveState::Loading);
}

TEST_F(ModelAssetSlotTest, MissingFileTurnsEverySharingRefFailed)
{
    ModelRef first(kMissingModelA);
    ModelRef copy = first;
    bool     bNotified = false;
    AssetManager::get()->loadModel(AssetManager::ModelLoadRequest{
        .filepath = kMissingModelA,
        .onReady  = [&](const std::shared_ptr<Model>& model) {
            EXPECT_EQ(model, nullptr);
            bNotified = true;
        },
    });

    ASSERT_TRUE(pumpUntilSettled(first._handle));
    EXPECT_EQ(first.getResolveState(), EAssetResolveState::Failed);
    EXPECT_EQ(copy.getResolveState(), EAssetResolveState::Failed);
    EXPECT_EQ(first.get(), nullptr);
    EXPECT_EQ(first._handle->generation, 1u);
    EXPECT_TRUE(bNotified);
    EXPECT_FALSE(AssetManager::get()->isModelLoaded(kMissingModelA));

    // A failed slot stays failed for new requests instead of re-decoding.
    ModelRef later(kMissingModelA);
    EXPECT_EQ(later._handle, first._handle);
    EXPECT_EQ(later.getResolveState(), EAssetResolveState::Failed);
}

TEST_F(ModelAssetSlotTest, InvalidateErasesTheEntryAndNextRequestBindsANewSlot)
{
    // Model invalidate drops the cache entry (a reload re-decodes through a
    // NEW slot): holders keep the old slot reading Failed until they rebind.
    ModelRef ref(kMissingModelA);
    ASSERT_TRUE(pumpUntilSettled(ref._handle));
    const AssetHandle<Model> slot = ref._handle;

    AssetManager::get()->invalidate(kMissingModelA);
    EXPECT_EQ(ref.getResolveState(), EAssetResolveState::Failed);
    EXPECT_EQ(slot->generation, 2u);

    ModelRef fresh(kMissingModelA);
    EXPECT_NE(fresh._handle, slot);
    ASSERT_TRUE(pumpUntilSettled(fresh._handle));
    EXPECT_EQ(fresh.getResolveState(), EAssetResolveState::Failed);
}

TEST_F(ModelAssetSlotTest, UnloadFailsHeldSlotAndNextRequestGetsANewOne)
{
    ModelRef ref(kMissingModelA);
    const AssetHandle<Model> slot = ref._handle;

    AssetManager::get()->unload(kMissingModelA);
    EXPECT_EQ(ref.getResolveState(), EAssetResolveState::Failed);

    ModelRef fresh(kMissingModelA);
    EXPECT_NE(fresh._handle, slot);
    ASSERT_TRUE(pumpUntilSettled(fresh._handle));
}

TEST_F(ModelAssetSlotTest, ClearFailsSlotsStillHeldByRefs)
{
    ModelRef ref(kMissingModelA);
    AssetManager::get()->clearCache();
    EXPECT_EQ(ref.getResolveState(), EAssetResolveState::Failed);
    EXPECT_EQ(ref.get(), nullptr);

    // The decode submitted before the clear must not update the slot again.
    for (int i = 0; i < 50; ++i) {
        TaskQueue::get().processMainThreadCallbacks();
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    EXPECT_EQ(ref._handle->generation, 1u);
}

TEST_F(ModelAssetSlotTest, CollectUnusedKeepsHeldSlotsAndDropsTheRest)
{
    ModelRef held(kMissingModelA);
    {
        ModelRef dropped(kMissingModelB);
        ASSERT_TRUE(pumpUntilSettled(dropped._handle));
    }
    ASSERT_TRUE(pumpUntilSettled(held._handle));

    EXPECT_EQ(AssetManager::get()->collectUnused(), 1u);
    EXPECT_FALSE(AssetManager::get()->isModelLoaded(kMissingModelA));
    EXPECT_FALSE(AssetManager::get()->isModelLoaded(kMissingModelB));
}

} // namespace
} // namespace ya
