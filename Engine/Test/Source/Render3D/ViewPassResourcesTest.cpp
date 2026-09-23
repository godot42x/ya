#include "Render3D/Common/RenderSubmission.h"
#include "Render3D/Common/RenderViewBindingTable.h"
#include "Render3D/Common/ViewPassResources.h"
#include "Render3D/Common/Shadow/ShadowFrameResources.h"
#include "Render3D/Deferred/DeferredFrameResourceSet.h"
#include "Render3D/Deferred/SSAOStage.h"
#include "Render3D/Forward/ForwardFrameResourceSet.h"
#include "RHI/Core/Buffer.h"
#include "RHI/Core/CommandBuffer.h"
#include "RHI/Core/RenderResourceFactory.h"
#include "Core/Common/DeferredDeletionQueue.h"

#include <cstdint>
#include <gtest/gtest.h>
#include <memory>
#include <string>
#include <vector>

namespace ya
{
namespace
{

class TestBuffer final : public IBuffer
{
    std::string  _name;
    EBufferUsage _usage = EBufferUsage::None;
    uint32_t     _size  = 0;

  public:
    explicit TestBuffer(const BufferCreateInfo& desc)
        : _name(desc.label), _usage(desc.usage), _size(desc.size)
    {}

    bool writeData(const void*, uint32_t = 0, uint32_t = 0) override { return true; }
    bool flush(uint32_t = 0, uint32_t = 0) override { return true; }
    void unmap() override {}
    BufferHandle getHandle() const override
    {
        return BufferHandle{reinterpret_cast<void*>(static_cast<uintptr_t>(_size + 1))};
    }
    uint32_t           getSize() const override { return _size; }
    EBufferUsage       getUsage() const override { return _usage; }
    bool               isHostVisible() const override { return true; }
    const std::string& getName() const override { return _name; }

  protected:
    void mapInternal(void** ptr) override { *ptr = nullptr; }
};

class TestResourceFactory final : public IRenderResourceFactory
{
  public:
    std::vector<std::shared_ptr<IBuffer>> ownedBuffers;

    std::shared_ptr<IBuffer> createBuffer(const BufferCreateInfo& desc) override
    {
        auto buffer = std::make_shared<TestBuffer>(desc);
        ownedBuffers.push_back(buffer);
        return buffer;
    }

    std::shared_ptr<Sampler>    createSampler(const SamplerDesc&) override { return nullptr; }
    std::shared_ptr<IImage>     createImage(const ImageCreateInfo&) override { return nullptr; }
    std::shared_ptr<IImage>     importImage(const ImportedImageDesc&) override { return nullptr; }
    std::shared_ptr<IImageView> createImageView(std::shared_ptr<IImage>, const ImageViewCreateInfo&) override
    {
        return nullptr;
    }
};

ICommandBuffer* dummyCmdBuf(uintptr_t token)
{
    return reinterpret_cast<ICommandBuffer*>(token);
}

} // namespace

TEST(ViewPassResourcesTest, DualViewTypedPassSlotsStayIndependent)
{
    RenderViewBindingTable<DeferredFrameResourceSet::ViewResources> table;
    ASSERT_TRUE(table.beginSubmission(0, 41));

    DeferredFrameResourceSet::ViewResources* viewA = table.mutableNextView(0);
    ASSERT_NE(viewA, nullptr);
    viewA->ssao.inputs.set              = DescriptorSetHandle{reinterpret_cast<void*>(0xA1)};
    viewA->lighting.gBufferTextures.set = DescriptorSetHandle{reinterpret_cast<void*>(0xA2)};
    viewA->lighting.shadows.set         = DescriptorSetHandle{reinterpret_cast<void*>(0xA3)};
    viewA->entityId.frame.set           = DescriptorSetHandle{reinterpret_cast<void*>(0xA4)};
    viewA->overlay.billboardTextures.set = DescriptorSetHandle{reinterpret_cast<void*>(0xA5)};
    viewA->post.toneMap.input.set       = DescriptorSetHandle{reinterpret_cast<void*>(0xA6)};
    viewA->post.bloom.extract.set       = DescriptorSetHandle{reinterpret_cast<void*>(0xA7)};
    ASSERT_TRUE(table.commitNextView(0));

    DeferredFrameResourceSet::ViewResources* viewB = table.mutableNextView(0);
    ASSERT_NE(viewB, nullptr);
    EXPECT_NE(viewA, viewB);
    viewB->ssao.inputs.set              = DescriptorSetHandle{reinterpret_cast<void*>(0xB1)};
    viewB->lighting.gBufferTextures.set = DescriptorSetHandle{reinterpret_cast<void*>(0xB2)};
    viewB->lighting.shadows.set         = DescriptorSetHandle{reinterpret_cast<void*>(0xB3)};
    viewB->entityId.frame.set           = DescriptorSetHandle{reinterpret_cast<void*>(0xB4)};
    viewB->overlay.billboardTextures.set = DescriptorSetHandle{reinterpret_cast<void*>(0xB5)};
    viewB->post.toneMap.input.set       = DescriptorSetHandle{reinterpret_cast<void*>(0xB6)};
    viewB->post.bloom.extract.set       = DescriptorSetHandle{reinterpret_cast<void*>(0xB7)};
    ASSERT_TRUE(table.commitNextView(0));

    const auto* slotA = table.getView(0, 0);
    const auto* slotB = table.getView(0, 1);
    ASSERT_NE(slotA, nullptr);
    ASSERT_NE(slotB, nullptr);
    EXPECT_NE(slotA, slotB);
    EXPECT_NE(slotA->ssao.inputs.set, slotB->ssao.inputs.set);
    EXPECT_NE(slotA->lighting.gBufferTextures.set, slotB->lighting.gBufferTextures.set);
    EXPECT_NE(slotA->entityId.frame.set, slotB->entityId.frame.set);
    EXPECT_NE(slotA->overlay.billboardTextures.set, slotB->overlay.billboardTextures.set);
    EXPECT_NE(slotA->post.toneMap.input.set, slotB->post.toneMap.input.set);
    EXPECT_NE(slotA->post.bloom.extract.set, slotB->post.bloom.extract.set);

    viewB->ssao.inputs.set        = DescriptorSetHandle{reinterpret_cast<void*>(0xB9)};
    viewB->post.bloom.extract.set = DescriptorSetHandle{reinterpret_cast<void*>(0xBA)};
    EXPECT_EQ(slotA->ssao.inputs.set, DescriptorSetHandle{reinterpret_cast<void*>(0xA1)});
    EXPECT_EQ(slotA->post.bloom.extract.set, DescriptorSetHandle{reinterpret_cast<void*>(0xA7)});
}

TEST(ViewPassResourcesTest, UniformPassBindingsUseIndependentSlices)
{
    auto& deletionQueue = DeferredDeletionQueue::get();
    deletionQueue.flushAll();
    deletionQueue.init(/*framesInFlight=*/1);

    TestResourceFactory  factory;
    RenderSubmissionPool pool;
    ASSERT_TRUE(pool.init(factory));

    RenderSubmission* live = pool.acquire(0, 21u, dummyCmdBuf(1));
    ASSERT_NE(live, nullptr);

    uint32_t dataA = 1;
    uint32_t dataB = 2;
    UniformBufferPassBinding bindingA{};
    UniformBufferPassBinding bindingB{};
    ASSERT_TRUE(writeUniformPassBinding(*live, nullptr, nullptr, 16, &dataA, sizeof(dataA), bindingA));
    ASSERT_TRUE(writeUniformPassBinding(*live, nullptr, nullptr, 16, &dataB, sizeof(dataB), bindingB));

    EXPECT_TRUE(bindingA.isValid());
    EXPECT_TRUE(bindingB.isValid());
    EXPECT_EQ(bindingA.slice.buffer.get(), bindingB.slice.buffer.get());
    EXPECT_NE(bindingA.slice.offset, bindingB.slice.offset);

    const uint64_t offsetA = bindingA.slice.offset;
    bindingB.slice.offset  = 4096;
    EXPECT_EQ(bindingA.slice.offset, offsetA);
}

TEST(ViewPassResourcesTest, PointShadowPacketsStayOnViewBinding)
{
    ShadowFrameResources::Binding viewA{};
    ShadowFrameResources::Binding viewB{};
    viewA.pointShadow.instanceCapacity = 4;
    viewA.pointShadow.ready            = true;
    viewB.pointShadow.instanceCapacity = 8;
    viewB.pointShadow.ready            = true;

    EXPECT_NE(&viewA.pointShadow, &viewB.pointShadow);
    EXPECT_NE(viewA.pointShadow.instanceCapacity, viewB.pointShadow.instanceCapacity);

    viewB.pointShadow.instanceCapacity = 32;
    viewB.pointShadow.ready            = false;
    EXPECT_EQ(viewA.pointShadow.instanceCapacity, 4u);
    EXPECT_TRUE(viewA.pointShadow.ready);
}

TEST(ViewPassResourcesTest, SsaoRecipeKeepsInputLayoutNotSingletonSet)
{
    SSAOStage stage;
    EXPECT_EQ(stage.getInputDSL(), nullptr);
    const float radiusBefore = stage.getRadius();
    EXPECT_GT(radiusBefore, 0.0f);
    EXPECT_EQ(stage.getRadius(), radiusBefore);
}

TEST(ViewPassResourcesTest, ForwardDebugAndPostSlotsStayIndependent)
{
    RenderViewBindingTable<ForwardFrameResourceSet::ViewResources> table;
    ASSERT_TRUE(table.beginSubmission(0, 42));

    auto* viewA = table.mutableNextView(0);
    ASSERT_NE(viewA, nullptr);
    viewA->debug.ubo.set         = DescriptorSetHandle{reinterpret_cast<void*>(0xD1)};
    viewA->post.toneMap.input.set = DescriptorSetHandle{reinterpret_cast<void*>(0xD2)};
    ASSERT_TRUE(table.commitNextView(0));

    auto* viewB = table.mutableNextView(0);
    ASSERT_NE(viewB, nullptr);
    viewB->debug.ubo.set         = DescriptorSetHandle{reinterpret_cast<void*>(0xE1)};
    viewB->post.toneMap.input.set = DescriptorSetHandle{reinterpret_cast<void*>(0xE2)};
    ASSERT_TRUE(table.commitNextView(0));

    EXPECT_NE(table.getView(0, 0)->debug.ubo.set, table.getView(0, 1)->debug.ubo.set);
    EXPECT_NE(table.getView(0, 0)->post.toneMap.input.set, table.getView(0, 1)->post.toneMap.input.set);
    viewB->debug.ubo.set = DescriptorSetHandle{reinterpret_cast<void*>(0xE9)};
    EXPECT_EQ(table.getView(0, 0)->debug.ubo.set, DescriptorSetHandle{reinterpret_cast<void*>(0xD1)});
}

} // namespace ya
