#include "Render3D/Forward/ForwardPipelineRecipe.h"

#include <gtest/gtest.h>

namespace ya
{

TEST(ForwardRecipeInputsTest, ResolvedViewWithoutFeaturesDeclaresNoOptionalTargets)
{
    // A default ResolvedView is the "nothing resolved yet" state: no optional
    // feature has claimed a target. The mandatory stream targets (color, depth,
    // display, entity id) are still declared, because the map always needs them.
    const ForwardPipelineRecipe::ResolvedView view{};
    const auto targets = view.collectViewTargets();

    ASSERT_EQ(targets.size(), 4u);
    EXPECT_EQ(targets[0].role, EViewAttachment::SceneColor);
    EXPECT_EQ(targets[1].role, EViewAttachment::SceneDepth);
    EXPECT_TRUE(targets[1].isDepth);
    EXPECT_EQ(targets[2].role, EViewAttachment::DisplayColor);
    EXPECT_EQ(targets[3].role, EViewAttachment::EntityId);
    for (const auto& target : targets) {
        EXPECT_NE(target.role, EViewAttachment::BloomExtract);
        EXPECT_NE(target.role, EViewAttachment::BloomBlur);
        EXPECT_NE(target.role, EViewAttachment::BloomComposite);
    }
}

TEST(ForwardRecipeInputsTest, ConfigAndPrepareDefaultsStayEmpty)
{
    const ForwardPipelineRecipe::Config config{};
    EXPECT_TRUE(config.bEnabled);
    EXPECT_EQ(config.viewColorFormat, EFormat::Undefined);
    EXPECT_EQ(config.viewDepthFormat, EFormat::Undefined);

    const ForwardPipelineRecipe::PrepareContext prepare{};
    EXPECT_EQ(prepare.submission, nullptr);
    EXPECT_EQ(prepare.frameBinding, nullptr);
    EXPECT_EQ(prepare.alignment, 1u);
}

TEST(ForwardRecipeInputsTest, BloomAddsExactlyItsThreeTargetsInStableOrder)
{
    ForwardPipelineRecipe::ResolvedView view{};
    view.opaque.colorFormat = EFormat::R16G16B16A16_SFLOAT;
    view.opaque.depthFormat = EFormat::D32_SFLOAT_S8_UINT;
    view.finalize.target = ViewTargetRequirement{
        .role   = EViewAttachment::DisplayColor,
        .format = EFormat::R8G8B8A8_UNORM,
        .usage  = EImageUsage::ColorAttachment | EImageUsage::Sampled | EImageUsage::TransferSrc,
    };
    view.entityId.target = ViewTargetRequirement{
        .role   = EViewAttachment::EntityId,
        .format = EFormat::R32_UINT,
        .usage  = EImageUsage::ColorAttachment | EImageUsage::TransferSrc,
    };

    const auto baseTargets = view.collectViewTargets();
    ASSERT_EQ(baseTargets.size(), 4u);

    view.bloom.extract = ViewTargetRequirement{
        .role   = EViewAttachment::BloomExtract,
        .format = EFormat::R16G16B16A16_SFLOAT,
        .usage  = EImageUsage::ColorAttachment | EImageUsage::Sampled,
    };
    view.bloom.blur = ViewTargetRequirement{
        .role   = EViewAttachment::BloomBlur,
        .format = EFormat::R16G16B16A16_SFLOAT,
        .usage  = EImageUsage::ColorAttachment | EImageUsage::Sampled,
    };
    view.bloom.composite = ViewTargetRequirement{
        .role   = EViewAttachment::BloomComposite,
        .format = EFormat::R16G16B16A16_SFLOAT,
        .usage  = EImageUsage::ColorAttachment | EImageUsage::Sampled,
    };

    const auto targets = view.collectViewTargets();
    ASSERT_EQ(targets.size(), 7u);
    // The optional roles append after the mandatory ones, in a stable order, so
    // the request builder publishes the same list every frame.
    EXPECT_EQ(targets[4].role, EViewAttachment::BloomExtract);
    EXPECT_EQ(targets[5].role, EViewAttachment::BloomBlur);
    EXPECT_EQ(targets[6].role, EViewAttachment::BloomComposite);
}

} // namespace ya
