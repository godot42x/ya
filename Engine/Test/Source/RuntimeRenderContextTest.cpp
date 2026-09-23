#include "GameRuntime/Render/RuntimeRenderContext.h"

#include "Render3D/Common/RenderFrameInputs.h"
#include "Render3D/Common/RenderViewOutput.h"
#include "Render3D/Common/RecordedFrame.h"
#include "Render3D/Common/SceneRenderScheduler.h"
#include "Render3D/Forward/ForwardRenderPipeline.h"
#include "Render3D/RenderDeviceState.h"
#include "RHI/Core/CommandBuffer.h"
#include "RHI/Core/RenderSurfaceContext.h"
#include "RHI/Core/RenderTexture.h"
#include "Scene/Core/Scene.h"

#include "RenderTestAccess.h"

#include <gtest/gtest.h>

#include <concepts>
#include <memory>
#include <type_traits>

namespace ya
{
namespace
{

constexpr Extent2D kWorldExtent     = {.width = 1280, .height = 720};
constexpr Extent2D kThumbnailExtent = {.width = 256, .height = 256};

// The shape checks are concepts on a type parameter rather than plain
// requires-expressions on `RenderDeviceState`: only a dependent expression turns
// "this member is missing" (or unreachable) into an unsatisfied constraint
// instead of a compile error in the test itself.
template <typename Device>
concept RecordsAWholeFrame = requires(Device& device, const RenderFramePlan& plan) { device.record(plan); };

template <typename Device>
concept PublishesFamilyResults = requires(Device& device, uint32_t flight) {
    device.publishFamilyResult(flight, ViewFamilyRenderResult{});
};

template <typename Device>
concept LooksUpAPresentTargetWithoutBuildingOne = requires(Device& device, IRenderSurfaceContext& surface) {
    device.findSurfacePresentation(surface);
};

std::shared_ptr<RenderTexture> makeAttachment()
{
    return std::make_shared<RenderTexture>();
}

RenderViewOutput makeOutput(SceneViewId                            viewId,
                            Extent2D                               extent,
                            const std::shared_ptr<RenderTexture>&  color,
                            const std::shared_ptr<RenderTexture>&  depth)
{
    RenderViewOutput output;
    output.desc.viewId      = viewId;
    output.desc.extent      = extent;
    output.desc.colorFormat = EFormat::R16G16B16A16_SFLOAT;
    output.desc.depthFormat = EFormat::D32_SFLOAT;
    output.color            = color;
    output.depth            = depth;
    output.entityId         = makeAttachment();
    return output;
}

/// A tick that declares `viewId` and nothing else, extracted the way the host
/// does it, so the plan is the scheduler's own grouping rather than a
/// hand-assembled list.
RenderFramePlan makeTickDeclaring(Scene& scene, SceneViewId viewId)
{
    SceneRenderScheduler scheduler;
    scheduler.beginTick(1);
    EXPECT_TRUE(scheduler.submit(SceneViewDesc{
        .scene      = &scene,
        .viewId     = viewId,
        .outputRect = {.pos = {0.0f, 0.0f}, .extent = {1280.0f, 720.0f}},
    }));

    RenderFramePlan plan;
    plan.sceneRender = buildSceneSnapshots(scheduler.seal(),
                                           [](Scene&) { return std::make_shared<const SceneSnapshot>(); });
    return plan;
}

/// The whole-frame entry point is the application's. If someone re-adds a
/// renderer-side one, this stops compiling -- which is the point: a renderer
/// that can record a whole frame is a renderer that owns the frame's
/// arrangement (which surface presents, which View is the display root).
TEST(RuntimeRenderContextTest, TheFrameEntryPointIsTheApplicationsNotTheRenderers)
{
    static_assert(!RecordsAWholeFrame<RenderDeviceState>,
                  "RenderDeviceState must not offer a whole-frame record(): that is the application's order");
    static_assert(std::is_same_v<decltype(std::declval<RuntimeRenderContext&>().record(
                                      std::declval<const RenderFramePlan&>())),
                                  RecordedFrame>);
}

/// The steps that order is written with are the renderer's, but each is a real
/// action rather than a nested whole-frame call: the application can name the
/// surface it presents through, prepare, open the flight, record the families,
/// keep what must outlive the submit, close, and seal. This pins that exact
/// seam -- making any of these private again breaks the application's order at
/// compile time, and a `record()`-shaped wrapper would show up as the missing
/// steps here.
TEST(RuntimeRenderContextTest, TheApplicationsOrderIsWrittenWithTheRenderersOwnSteps)
{
    using Device = RenderDeviceState;

    static_assert(requires(Device& device, const RenderFramePlan& plan) { device.prepareFrameRecord(plan); });
    static_assert(requires(Device& device, const RenderFramePlan& plan) {
        { device.beginFrameCommandBuffer(plan, std::declval<std::shared_ptr<ICommandBuffer>&>()) } -> std::same_as<bool>;
    });
    static_assert(requires(Device& device, const RenderFramePlan& plan) { device.recordViewFamilies(plan); });
    static_assert(requires(Device& device) {
        device.retainPublishedViewOutputs(0u, static_cast<ICommandBuffer*>(nullptr));
    });
    static_assert(requires(Device& device) { device.endFrameCommandBuffer(static_cast<ICommandBuffer*>(nullptr)); });
    static_assert(requires(Device& device) {
        { device.sealFrame(0u, static_cast<ICommandBuffer*>(nullptr)) } -> std::same_as<RecordedFrame>;
    });
    static_assert(requires(Device& device, IRenderSurfaceContext& surface) {
        device.acquireSurfacePresentation(surface);
    });
    static_assert(requires(Device& device) { device.getLiveSubmission(0u); });
    static_assert(!PublishesFamilyResults<RenderDeviceState>,
                  "publishing is the family-recording step's own business, not a host step");
    static_assert(!LooksUpAPresentTargetWithoutBuildingOne<RenderDeviceState>,
                  "looking a present target up without being willing to build one is not a host step");
}

/// The order's first two steps, on the one path a test can drive without a real
/// command buffer. A frame whose present the host did not acquire is still a
/// frame the tick declared Views for, and the old order prepared before it
/// decided whether to record -- so a View this tick stopped declaring is dropped
/// from the pipeline even though no command is written.
///
/// Boundary: this pins that `RuntimeRenderContext::record` prepares (and
/// therefore evicts) *before* the "is there anything to record" gate. It does
/// not pin the rest of the sequence -- the families, compose and seal steps need
/// a real command buffer, so those stay something the readable order in
/// `RuntimeRenderContext.cpp` guarantees.
TEST(RuntimeRenderContextTest, ARefusedFrameStillPreparesTheTickBeforeItGivesUp)
{
    Scene scene("Authoring");

    auto              pipeline = std::make_shared<ForwardRenderPipeline>();
    RenderDeviceState device;
    RenderDeviceStateTestAccess::installActivePipeline(device, pipeline);

    // Two Views are published, as if the previous tick had declared both; this
    // tick declares only one of them.
    ForwardRenderPipelineTestAccess::publishViewResources(
        *pipeline, makeOutput(11, kWorldExtent, makeAttachment(), makeAttachment()), kWorldExtent, toMask(ERenderFeature::Game));
    ForwardRenderPipelineTestAccess::publishViewResources(
        *pipeline,
        makeOutput(12, kThumbnailExtent, makeAttachment(), makeAttachment()),
        kThumbnailExtent,
        toMask(ERenderFeature::Game));
    ASSERT_NE(pipeline->viewResourcesFor(12), nullptr);

    RuntimeRenderContext context{device};

    RenderFramePlan plan       = makeTickDeclaring(scene, 11);
    plan.frame.flightIndex     = 0;
    plan.present.surface       = nullptr; // the host did not acquire this frame
    plan.present.imageIndex    = -1;

    const RecordedFrame recorded = context.record(plan);

    // Nothing was recorded, so the host submits an empty frame and presents the
    // image it acquired.
    EXPECT_FALSE(recorded.valid());
    // ... and the tick was still prepared: the View it stopped declaring is gone.
    EXPECT_EQ(pipeline->viewResourcesFor(12), nullptr);
    // The View it still declares is untouched.
    EXPECT_NE(pipeline->viewResourcesFor(11), nullptr);
}

/// The gate itself: a plan with no acquired present opens no recording at all.
/// The host's `submitRecordedFrame` submits an empty frame in that case, so the
/// renderer must not have left a half-open submission behind for that flight.
TEST(RuntimeRenderContextTest, APlanWithoutAnAcquiredPresentOpensNoRecording)
{
    RenderDeviceState    device;
    RuntimeRenderContext context{device};

    const RecordedFrame recorded = context.record(RenderFramePlan{});

    EXPECT_FALSE(recorded.valid());
    EXPECT_EQ(recorded.commandBuffer, nullptr);
    EXPECT_EQ(device.getLiveSubmission(0), nullptr);
}

} // namespace
} // namespace ya
