#include "Core/Async/TaskQueue.h"
#include "Core/Common/AssetRef.h"
#include "Core/Common/TextureSlot.h"
#include "Core/System/VirtualFileSystem.h"
#include "RHI/Core/Image.h"
#include "RHI/Core/Texture.h"
#include "RHI/Render.h"
#include "Resource/AssetManager.h"

#include <gtest/gtest.h>

#include <chrono>
#include <thread>

namespace ya
{
namespace
{

// Enough of a backend for AssetManager to accept load requests. Decoding a
// missing file fails before any upload, so none of these are ever called.
struct NullRender final : IRender
{
    void                           destroy() override {}
    void                           setShaderStorage(std::shared_ptr<ShaderStorage>) override {}
    std::shared_ptr<ShaderStorage> getShaderStorage() override { return {}; }
    void allocateCommandBuffers(uint32_t, std::vector<std::shared_ptr<ICommandBuffer>>&) override {}
    void waitIdle() override {}
    ICommandBuffer*       beginIsolateCommands(const std::string&) override { return nullptr; }
    void                  endIsolateCommands(ICommandBuffer*) override {}
    IDescriptorSetHelper* getDescriptorHelper() override { return nullptr; }
    void  submitToQueue(const std::vector<void*>&, const std::vector<void*>&, const std::vector<void*>&, void*) override {}
    void* createSemaphore(const char*) override { return nullptr; }
    void  destroySemaphore(void*) override {}
};

struct NullImage final : IImage
{
    ImageHandle     getHandle() const override { return {}; }
    uint32_t        getWidth() const override { return 4; }
    uint32_t        getHeight() const override { return 4; }
    EFormat::T      getFormat() const override { return EFormat::R8G8B8A8_UNORM; }
    uint32_t        getMipLevels() const override { return 1; }
    uint32_t        getArrayLayers() const override { return 1; }
    EImageUsage::T  getUsage() const override { return EImageUsage::Sampled; }
    EImageLayout::T getCompatibilityLayout() const override { return EImageLayout::ShaderReadOnlyOptimal; }
    void            setDebugName(const std::string&) override {}
};

struct NullImageView final : IImageView
{
    ImageViewHandle getHandle() const override { return {}; }
    void            setDebugName(const std::string&) override {}
};

std::shared_ptr<Texture> makeCpuOnlyTexture(const std::string& label)
{
    return Texture::wrap(std::make_shared<NullImage>(), std::make_shared<NullImageView>(), label);
}

// Paths that never exist, so every async decode fails on the worker.
constexpr const char* kMissingA = "Content/Textures/__slot_test_missing_a.png";
constexpr const char* kMissingB = "Content/Textures/__slot_test_missing_b.png";

class TextureAssetSlotTest : public ::testing::Test
{
  protected:
    NullRender render;

    void SetUp() override
    {
        // Loading resolves meta through the VFS, which hosts mount first.
        if (!VirtualFileSystem::get()) {
            VirtualFileSystem::init();
        }
        // Completion callbacks run inline; an App from an earlier test may
        // have left its frame sink installed.
        AssetManager::setFrameTaskSink({});
        AssetManager::get()->clearTextures();
        AssetManager::get()->setRender(&render);
        TaskQueue::get().start(1);
    }

    void TearDown() override
    {
        AssetManager::get()->clearTextures();
        AssetManager::get()->setRender(nullptr);
    }

    static bool pumpUntilSettled(const AssetHandle<Texture>& handle)
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

TEST_F(TextureAssetSlotTest, SamePathSharesOneSlotAndCopiesShareIt)
{
    TextureRef first(kMissingA);
    TextureRef second;
    second.setPath(kMissingA);
    TextureRef copy = first;
    TextureRef other(kMissingB);

    ASSERT_NE(first._handle, nullptr);
    EXPECT_EQ(first._handle, second._handle);
    EXPECT_EQ(first._handle, copy._handle);
    EXPECT_NE(first._handle, other._handle);
    EXPECT_EQ(first.getResolveState(), EAssetResolveState::Loading);
}

TEST_F(TextureAssetSlotTest, MissingFileTurnsEverySharingRefFailed)
{
    TextureRef first(kMissingA);
    TextureRef copy = first;
    bool       bNotified = false;
    AssetManager::get()->loadTexture(AssetManager::TextureLoadRequest{
        .filepath = kMissingA,
        .onReady  = [&](const std::shared_ptr<Texture>& texture) {
            EXPECT_EQ(texture, nullptr);
            bNotified = true;
        },
    });

    ASSERT_TRUE(pumpUntilSettled(first._handle));
    EXPECT_EQ(first.getResolveState(), EAssetResolveState::Failed);
    EXPECT_EQ(copy.getResolveState(), EAssetResolveState::Failed);
    EXPECT_EQ(first.get(), nullptr);
    EXPECT_EQ(first._handle->generation, 1u);
    EXPECT_TRUE(bNotified);
    EXPECT_TRUE(AssetManager::get()->isTextureLoadFailed(kMissingA));

    // A failed slot stays failed for new requests instead of re-decoding.
    TextureRef later(kMissingA);
    EXPECT_EQ(later._handle, first._handle);
    EXPECT_EQ(later.getResolveState(), EAssetResolveState::Failed);
}

TEST_F(TextureAssetSlotTest, ReloadRefillsTheSameSlot)
{
    TextureRef ref(kMissingA);
    ASSERT_TRUE(pumpUntilSettled(ref._handle));
    const AssetHandle<Texture> slot = ref._handle;

    AssetManager::get()->invalidate(kMissingA);
    EXPECT_EQ(ref.getResolveState(), EAssetResolveState::Loading);
    ASSERT_TRUE(pumpUntilSettled(ref._handle));

    EXPECT_EQ(ref._handle, slot);
    EXPECT_EQ(ref.getResolveState(), EAssetResolveState::Failed);
    EXPECT_EQ(slot->generation, 2u);
}

TEST_F(TextureAssetSlotTest, UnloadFailsHeldSlotAndNextRequestGetsANewOne)
{
    TextureRef ref(kMissingA);
    const AssetHandle<Texture> slot = ref._handle;

    AssetManager::get()->unload(kMissingA);
    EXPECT_EQ(ref.getResolveState(), EAssetResolveState::Failed);

    TextureRef fresh(kMissingA);
    EXPECT_NE(fresh._handle, slot);
    ASSERT_TRUE(pumpUntilSettled(fresh._handle));
}

TEST_F(TextureAssetSlotTest, ClearFailsSlotsStillHeldByRefs)
{
    TextureRef ref(kMissingA);
    AssetManager::get()->clearTextures();
    EXPECT_EQ(ref.getResolveState(), EAssetResolveState::Failed);
    EXPECT_EQ(ref.get(), nullptr);

    // The decode submitted before the clear must not refill the slot.
    for (int i = 0; i < 50; ++i) {
        TaskQueue::get().processMainThreadCallbacks();
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    EXPECT_EQ(ref._handle->generation, 1u);
}

TEST_F(TextureAssetSlotTest, CollectUnusedKeepsHeldSlotsAndDropsTheRest)
{
    TextureRef held(kMissingA);
    {
        TextureRef dropped(kMissingB);
        ASSERT_TRUE(pumpUntilSettled(dropped._handle));
    }
    ASSERT_TRUE(pumpUntilSettled(held._handle));

    EXPECT_EQ(AssetManager::get()->collectUnused(), 1u);
    EXPECT_TRUE(AssetManager::get()->isTextureLoadFailed(kMissingA));
    EXPECT_FALSE(AssetManager::get()->isTextureLoadFailed(kMissingB));
}

TEST_F(TextureAssetSlotTest, RegisteredTextureIsReadyByNameUntilUnused)
{
    AssetManager::get()->registerTexture("__slot_test_registered", makeCpuOnlyTexture("registered"));
    EXPECT_NE(AssetManager::get()->getTextureByName("__slot_test_registered"), nullptr);

    EXPECT_EQ(AssetManager::get()->collectUnused(), 1u);
    EXPECT_EQ(AssetManager::get()->getTextureByName("__slot_test_registered"), nullptr);
}

TEST_F(TextureAssetSlotTest, EmptyPathBindsNothing)
{
    TextureRef ref(kMissingA);
    ref.setPath("");
    EXPECT_EQ(ref._handle, nullptr);
    EXPECT_EQ(ref.getResolveState(), EAssetResolveState::Empty);

    TextureSlot slot;
    EXPECT_TRUE(slot.isReady());
    EXPECT_FALSE(slot.isEnabledEffective());
}

TEST_F(TextureAssetSlotTest, WithoutRenderBackendRefsReadFailedAndNothingIsCached)
{
    AssetManager::get()->setRender(nullptr);
    TextureRef first(kMissingA);
    TextureRef second(kMissingB);

    EXPECT_EQ(first.getResolveState(), EAssetResolveState::Failed);
    EXPECT_EQ(second.getResolveState(), EAssetResolveState::Failed);
    EXPECT_EQ(AssetManager::get()->getStats().textureCount, 0u);
    EXPECT_FALSE(AssetManager::get()->isTextureLoadFailed(kMissingA));
}

} // namespace
} // namespace ya
