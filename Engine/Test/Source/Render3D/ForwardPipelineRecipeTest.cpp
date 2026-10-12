#include "Render3D/Forward/ForwardPipelineRecipe.h"

#include "Render3D/Common/RenderFrameInputs.h"

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

TEST(ForwardRecipeLifecycleTest, EmptyPlanResolvesToNoViewsAndIsNotAFailure)
{
    // The legal empty case: no View declared one, so the composition is empty
    // and that is a resolved composition, not an error. A UI-only or headless
    // frame must not be answered with "the pipeline failed".
    const ForwardPipelineRecipe::Config config{};
    const RenderFramePlan plan{};

    const auto resolved = ForwardPipelineRecipe::resolve(config, plan);
    ASSERT_TRUE(resolved.isOk());
    EXPECT_TRUE(resolved.value().empty());
}

TEST(ForwardRecipeLifecycleTest, DisabledCompositionIsAnErrorNotAnEmptyResolve)
{
    // A composition handed a disabled config is a wiring bug, not a legal
    // "nothing to draw": callers read the error and fail the frame instead of
    // recording a View with no targets.
    ForwardPipelineRecipe::Config config{};
    config.bEnabled = false;

    const RenderFramePlan plan{};
    const auto resolved = ForwardPipelineRecipe::resolve(config, plan);
    ASSERT_TRUE(resolved.isErr());
    EXPECT_EQ(resolved.error(), FeatureError::InvalidInput);
}

TEST(ForwardRecipeLifecycleTest, MissingFrameBindingFailsPreparationInsteadOfBindingNothing)
{
    // Preparing without the View's binding must fail: a pass that bound empty
    // sets would draw with whatever the driver left there.
    ForwardPipelineRecipe::ResolvedView view{};
    view.viewId = 5;
    view.opaque.bEnabled = true;

    const ForwardPipelineRecipe::Stages stages{};
    const ForwardPipelineRecipe::PrepareContext ctx{}; // no submission, no binding
    const auto prepared = ForwardPipelineRecipe::prepareBindings(view, stages, ctx);
    ASSERT_TRUE(prepared.isErr());
    EXPECT_EQ(prepared.error(), FeatureError::MissingContext);
}


TEST(ForwardRecipeLifecycleTest, DescribeStatesEachFeatureAndItsTargets)
{
    // The dump is the map talking: one line per feature with its decision, plus
    // the declared targets. A reader lines it up against the graph trace, so it
    // must name features and carry owning strings only.
    ForwardPipelineRecipe::ResolvedView view{};
    view.viewId     = 31;
    view.viewExtent = {.width = 320, .height = 200};
    view.opaque.bEnabled = true;
    view.finalize.bEnabled = true;
    view.bloom.bEnabled = true;

    const std::string text = view.describe();
    EXPECT_NE(text.find("Forward View 31"), std::string::npos);
    EXPECT_NE(text.find("320x200"), std::string::npos);
    EXPECT_NE(text.find("opaque"), std::string::npos);
    EXPECT_NE(text.find("shadow"), std::string::npos);
    EXPECT_NE(text.find("bloom"), std::string::npos);
    EXPECT_NE(text.find("targets:"), std::string::npos);
}

} // namespace ya
