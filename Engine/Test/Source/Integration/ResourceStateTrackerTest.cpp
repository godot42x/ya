#include "RHI/Core/Image.h"
#include "RHI/Core/ResourceStateTracker.h"
#include "RHI/Backend/Vulkan/VulkanUtils.h"

#include <gtest/gtest.h>
#include <unordered_map>

namespace ya
{

namespace
{

class TestImage final : public IImage
{
  private:
    EImageLayout::T _compatibilityLayout;
    uint32_t        _mipLevels;
    uint32_t        _arrayLayers;
    std::unordered_map<uint64_t, EImageLayout::T> _subresourceLayouts;

    static uint64_t makeSubresourceKey(uint32_t aspect, uint32_t mip, uint32_t layer)
    {
        return (static_cast<uint64_t>(aspect) << 42u) |
               (static_cast<uint64_t>(mip) << 21u) |
               static_cast<uint64_t>(layer);
    }

  public:
    TestImage(EImageLayout::T layout, uint32_t mipLevels, uint32_t arrayLayers)
        : _compatibilityLayout(layout), _mipLevels(mipLevels), _arrayLayers(arrayLayers)
    {}

    ImageHandle getHandle() const override { return {}; }
    uint32_t getWidth() const override { return 64; }
    uint32_t getHeight() const override { return 64; }
    EFormat::T getFormat() const override { return EFormat::R8G8B8A8_UNORM; }
    uint32_t getMipLevels() const override { return _mipLevels; }
    uint32_t getArrayLayers() const override { return _arrayLayers; }
    EImageUsage::T getUsage() const override { return EImageUsage::Sampled; }
    EImageLayout::T getCompatibilityLayout() const override { return _compatibilityLayout; }
    EImageLayout::T getCompatibilityLayout(uint32_t aspect, uint32_t mip, uint32_t layer) const override
    {
        if (const auto it = _subresourceLayouts.find(makeSubresourceKey(aspect, mip, layer)); it != _subresourceLayouts.end()) {
            return it->second;
        }
        return _compatibilityLayout;
    }
    void setDebugName(const std::string&) override {}
    void setCompatibilityLayout(EImageLayout::T layout) { _compatibilityLayout = layout; }
    void setCompatibilityLayout(uint32_t aspect, uint32_t mip, uint32_t layer, EImageLayout::T layout)
    {
        _subresourceLayouts[makeSubresourceKey(aspect, mip, layer)] = layout;
    }
};

} // namespace

TEST(ResourceStateTrackerTest, WholeImageTransitionUsesOneRangeAndDeduplicates)
{
    TestImage            image(EImageLayout::Undefined, 4, 6);
    ResourceStateTracker tracker;

    const auto transitions = tracker.transition(image, EImageLayout::ShaderReadOnlyOptimal);
    ASSERT_EQ(transitions.size(), 1u);
    EXPECT_EQ(transitions[0].oldState.layout, EImageLayout::Undefined);
    EXPECT_EQ(transitions[0].newState.layout, EImageLayout::ShaderReadOnlyOptimal);
    EXPECT_EQ(transitions[0].range.levelCount, 4u);
    EXPECT_EQ(transitions[0].range.layerCount, 6u);
    EXPECT_TRUE(tracker.transition(image, EImageLayout::ShaderReadOnlyOptimal).empty());
}

TEST(ResourceStateTrackerTest, SubresourceTransitionDoesNotOverwriteOtherLayers)
{
    TestImage            image(EImageLayout::ShaderReadOnlyOptimal, 2, 6);
    ResourceStateTracker tracker;
    const ImageSubresourceRange faceRange{
        .aspectMask     = EImageAspect::Color,
        .baseMipLevel   = 1,
        .levelCount     = 1,
        .baseArrayLayer = 2,
        .layerCount     = 1,
    };

    const auto faceTransitions = tracker.transition(image, EImageLayout::ColorAttachmentOptimal, &faceRange);
    ASSERT_EQ(faceTransitions.size(), 1u);
    EXPECT_EQ(faceTransitions[0].range.baseMipLevel, 1u);
    EXPECT_EQ(faceTransitions[0].range.baseArrayLayer, 2u);

    image.setCompatibilityLayout(EImageLayout::ColorAttachmentOptimal);

    const auto wholeTransitions = tracker.transition(image, EImageLayout::TransferSrc);
    EXPECT_GT(wholeTransitions.size(), 1u);

    bool bFoundChangedFace = false;
    bool bFoundUnchangedResources = false;
    for (const auto& transition : wholeTransitions) {
        bFoundChangedFace |= transition.oldState.layout == EImageLayout::ColorAttachmentOptimal;
        bFoundUnchangedResources |= transition.oldState.layout == EImageLayout::ShaderReadOnlyOptimal;
    }
    EXPECT_TRUE(bFoundChangedFace);
    EXPECT_TRUE(bFoundUnchangedResources);
}

TEST(ResourceStateTrackerTest, ResetFallsBackToImageCompatibilityLayout)
{
    TestImage            image(EImageLayout::PresentSrcKHR, 1, 1);
    ResourceStateTracker tracker;

    tracker.setLayout(image, EImageLayout::ColorAttachmentOptimal);
    tracker.reset();

    const auto transitions = tracker.transition(image, EImageLayout::TransferSrc);
    ASSERT_EQ(transitions.size(), 1u);
    EXPECT_EQ(transitions[0].oldState.layout, EImageLayout::PresentSrcKHR);
}

TEST(ResourceStateTrackerTest, ResetUsesPerSubresourceCompatibilitySeedWhenAvailable)
{
    TestImage            image(EImageLayout::ShaderReadOnlyOptimal, 2, 4);
    ResourceStateTracker tracker;

    image.setCompatibilityLayout(EImageAspect::Color, 1, 2, EImageLayout::ColorAttachmentOptimal);

    const auto transitions = tracker.transition(image, EImageLayout::TransferSrc);
    ASSERT_EQ(transitions.size(), 4u);

    bool bFoundOverriddenSubresource = false;
    bool bFoundDefaultSeedSubresource = false;
    for (const auto& transition : transitions) {
        bFoundOverriddenSubresource |=
            transition.oldState.layout == EImageLayout::ColorAttachmentOptimal &&
            transition.range.baseMipLevel == 1u &&
            transition.range.baseArrayLayer == 2u;
        bFoundDefaultSeedSubresource |= transition.oldState.layout == EImageLayout::ShaderReadOnlyOptimal;
    }

    EXPECT_TRUE(bFoundOverriddenSubresource);
    EXPECT_TRUE(bFoundDefaultSeedSubresource);
}

TEST(ResourceStateTrackerTest, ValidateLayoutReportsChangedSubresourceRange)
{
    TestImage            image(EImageLayout::ShaderReadOnlyOptimal, 2, 4);
    ResourceStateTracker tracker;
    const ImageSubresourceRange changedRange{
        .aspectMask     = EImageAspect::Color,
        .baseMipLevel   = 1,
        .levelCount     = 1,
        .baseArrayLayer = 2,
        .layerCount     = 1,
    };

    tracker.setLayout(image, EImageLayout::ColorAttachmentOptimal, &changedRange);

    const auto mismatches = tracker.validateLayout(image, EImageLayout::ShaderReadOnlyOptimal);
    ASSERT_EQ(mismatches.size(), 1u);
    EXPECT_EQ(mismatches[0].actualLayout, EImageLayout::ColorAttachmentOptimal);
    EXPECT_EQ(mismatches[0].range.baseMipLevel, 1u);
    EXPECT_EQ(mismatches[0].range.baseArrayLayer, 2u);
    EXPECT_EQ(mismatches[0].range.layerCount, 1u);
}

TEST(ResourceStateTrackerTest, ValidateLayoutAcceptsMatchingExplicitOldLayout)
{
    TestImage            image(EImageLayout::TransferDst, 1, 1);
    ResourceStateTracker tracker;

    const auto mismatches = tracker.validateLayout(image, EImageLayout::TransferDst);
    EXPECT_TRUE(mismatches.empty());
}

TEST(ResourceStateTrackerTest, ResourceStateDefinitionsUseExplicitDefinedFlag)
{
    BufferResourceState bufferState;
    ImageResourceState  imageState;

    EXPECT_FALSE(bufferState.isDefined());
    EXPECT_FALSE(imageState.isDefined());

    bufferState.access = EResourceAccess::TransferWrite;
    imageState.layout  = EImageLayout::ShaderReadOnlyOptimal;

    EXPECT_TRUE(bufferState.isDefined());
    EXPECT_TRUE(imageState.isDefined());
}

TEST(ResourceStateTrackerTest, SetStateTracksFullImageResourceStatePerSubresource)
{
    TestImage            image(EImageLayout::Undefined, 2, 2);
    ResourceStateTracker tracker;
    const ImageSubresourceRange range{
        .aspectMask     = EImageAspect::Color,
        .baseMipLevel   = 1,
        .levelCount     = 1,
        .baseArrayLayer = 1,
        .layerCount     = 1,
    };

    ImageResourceState state;
    state.layout = EImageLayout::TransferDst;
    state.stages = EPipelineStage::Transfer;
    state.access = EResourceAccess::TransferWrite;

    tracker.setState(image, state, &range);

    const auto mismatches = tracker.validateLayout(image, EImageLayout::Undefined);
    ASSERT_EQ(mismatches.size(), 1u);
    EXPECT_EQ(mismatches[0].actualLayout, EImageLayout::TransferDst);
    EXPECT_EQ(mismatches[0].range.baseMipLevel, 1u);
    EXPECT_EQ(mismatches[0].range.baseArrayLayer, 1u);
}

TEST(ResourceStateTrackerTest, AttachmentResourceStatesMapToVulkanAttachmentBits)
{
    EXPECT_EQ(EPipelineStage::toVk(EPipelineStage::ColorAttachmentOutput), VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT);
    EXPECT_EQ(EPipelineStage::toVk(EPipelineStage::EarlyFragmentTests | EPipelineStage::LateFragmentTests),
              VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT);
    EXPECT_EQ(EResourceAccess::toVk(EResourceAccess::ColorAttachmentRead | EResourceAccess::ColorAttachmentWrite),
              VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT);
    EXPECT_EQ(EResourceAccess::toVk(EResourceAccess::DepthStencilAttachmentRead | EResourceAccess::DepthStencilAttachmentWrite),
              VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT);
}

} // namespace ya
