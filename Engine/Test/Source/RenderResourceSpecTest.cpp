#include "Render/Core/Buffer.h"
#include "Render/Core/RenderResourceFactory.h"
#include "Render/RenderDefines.h"

#include <gtest/gtest.h>

namespace ya
{

TEST(BufferCreateInfoSpecTest, ComparesIdentityAndIgnoresTransientDataPointer)
{
    BufferCreateInfo lhs{
        .label       = "Test.Buffer",
        .usage       = EBufferUsage::UniformBuffer,
        .data        = reinterpret_cast<void*>(0x1234),
        .size        = 256,
        .memoryUsage = EMemoryUsage::CpuToGpu,
    };
    BufferCreateInfo rhs = lhs;
    rhs.data             = reinterpret_cast<void*>(0x5678);

    EXPECT_TRUE(isSameBufferCreateInfo(lhs, rhs));

    rhs.size = 512;
    EXPECT_FALSE(isSameBufferCreateInfo(lhs, rhs));
    rhs.size = lhs.size;

    rhs.usage = EBufferUsage::StorageBuffer;
    EXPECT_FALSE(isSameBufferCreateInfo(lhs, rhs));
    rhs.usage = lhs.usage;

    rhs.memoryUsage = EMemoryUsage::GpuOnly;
    EXPECT_FALSE(isSameBufferCreateInfo(lhs, rhs));
    rhs.memoryUsage = lhs.memoryUsage;

    rhs.label = "Test.Other";
    EXPECT_FALSE(isSameBufferCreateInfo(lhs, rhs));
}

TEST(ImageCreateInfoSpecTest, ComparesIdentityAndIgnoresQueueFamilySharingContext)
{
    uint32_t queueFamiliesA[] = {0u, 1u};
    uint32_t queueFamiliesB[] = {2u, 3u};

    ImageCreateInfo lhs{
        .label       = "Test.Image",
        .format      = EFormat::R8G8B8A8_UNORM,
        .extent      = {.width = 64, .height = 32, .depth = 1},
        .mipLevels   = 4,
        .arrayLayers = 6,
        .samples     = ESampleCount::Sample_4,
        .usage       = static_cast<EImageUsage::T>(EImageUsage::Sampled | EImageUsage::ColorAttachment),
        .queueFamilyIndexCount = 2,
        .pQueueFamilyIndices   = queueFamiliesA,
        .initialLayout         = EImageLayout::Undefined,
        .flags                 = EImageCreateFlag::None,
    };
    ImageCreateInfo rhs = lhs;
    rhs.pQueueFamilyIndices = queueFamiliesB;

    EXPECT_TRUE(isSameImageCreateInfo(lhs, rhs));

    rhs.extent.height = 16;
    EXPECT_FALSE(isSameImageCreateInfo(lhs, rhs));
    rhs.extent.height = lhs.extent.height;

    rhs.samples = ESampleCount::Sample_1;
    EXPECT_FALSE(isSameImageCreateInfo(lhs, rhs));
    rhs.samples = lhs.samples;

    rhs.usage = static_cast<EImageUsage::T>(EImageUsage::Sampled | EImageUsage::DepthStencilAttachment);
    EXPECT_FALSE(isSameImageCreateInfo(lhs, rhs));
    rhs.usage = lhs.usage;

    rhs.flags = EImageCreateFlag::CubeCompatible;
    EXPECT_FALSE(isSameImageCreateInfo(lhs, rhs));
}

TEST(SamplerDescSpecTest, ComparisonIncludesBorderColorAndLabel)
{
    SamplerDesc lhs{};
    SamplerDesc rhs = lhs;
    EXPECT_TRUE(lhs == rhs);

    rhs.label = "Test.Sampler";
    EXPECT_FALSE(lhs == rhs);
    rhs.label = lhs.label;

    rhs.borderColor.type = SamplerDesc::EBorderColor::FloatOpaqueWhite;
    EXPECT_FALSE(lhs == rhs);
    rhs.borderColor.type = lhs.borderColor.type;

    rhs.minFilter = EFilter::Nearest;
    EXPECT_FALSE(lhs == rhs);
    rhs.minFilter = lhs.minFilter;

    rhs.addressModeU = ESamplerAddressMode::ClampToEdge;
    EXPECT_FALSE(lhs == rhs);
}

TEST(ImageViewSpecTest, ComparisonCoversViewIdentityAndLabel)
{
    ImageViewCreateInfo lhs{
        .label       = "Test.View",
        .viewType    = EImageViewType::ViewCube,
        .aspectFlags = EImageAspect::Color,
        .levelCount  = 1,
        .layerCount  = 6,
    };
    ImageViewCreateInfo rhs = lhs;
    EXPECT_TRUE(isSameImageViewCreateInfo(lhs, rhs));

    rhs.layerCount = 1;
    rhs.viewType   = EImageViewType::View2D;
    EXPECT_FALSE(isSameImageViewCreateInfo(lhs, rhs));
    rhs = lhs;

    rhs.label = "Test.Other";
    EXPECT_FALSE(isSameImageViewCreateInfo(lhs, rhs));
}

} // namespace ya
