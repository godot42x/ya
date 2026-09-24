#include "GameRuntime/Render/RuntimeRenderContext.h"

#include "Render3D/Common/FrameRecordExtensions.h"
#include "Render3D/Common/RenderFrameInputs.h"
#include "Render3D/Common/RecordedFrame.h"
#include "Render3D/RenderDeviceState.h"
#include "RHI/Core/CommandBuffer.h"
#include "RHI/Core/RenderSurfaceContext.h"
#include "RHI/Core/SurfaceId.h"

#include <gtest/gtest.h>

#include <concepts>
#include <memory>
#include <type_traits>

namespace ya
{
namespace
{

// The shape checks are concepts on a type parameter rather than plain
// requires-expressions on `RenderDeviceState`: only a dependent expression turns
// "this member is missing" (or unreachable) into an unsatisfied constraint
// instead of a compile error in the test itself.
template <typename Device>
concept RecordsAWholeFrame = requires(Device& device, const RenderFramePlan& plan) { device.record(plan); };

template <typename Context>
concept RecordsWithAnExplicitHostStageArg =
    requires(Context& context, const RenderFramePlan& plan, IFrameRecordExtensions* extensions) {
        context.record(plan, extensions);
    };

template <typename Device>
concept PublishesFamilyResults = requires(Device& device, uint32_t flight) {
    device.publishFamilyResult(flight, ViewFamilyRenderResult{});
};

template <typename Device>
concept LooksUpAPresentTargetWithoutBuildingOne = requires(Device& device, IRenderSurfaceContext& surface) {
    device.findSurfacePresentation(surface);
};

/// The whole-frame entry point is the application's. If someone re-adds a
/// renderer-side one, this stops compiling -- which is the point: a renderer
/// that can record a whole frame is a renderer that owns the frame's
/// arrangement (which surface presents, which View is the display root).
TEST(RuntimeRenderContextTest, TheFrameEntryPointIsTheApplicationsNotTheRenderers)
{
    static_assert(!RecordsAWholeFrame<RenderDeviceState>,
                  "RenderDeviceState must not offer a whole-frame record(): that is the application's order");
    static_assert(std::is_same_v<decltype(std::declval<RuntimeRenderContext&>().record(
                                      std::declval<const RenderFramePlan&>(),
                                      std::declval<IFrameRecordExtensions*>())),
                                  RecordedFrame>);
    // The plan is values; the host's record stages ride on the call, not on it.
    static_assert(RecordsWithAnExplicitHostStageArg<RuntimeRenderContext>);
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
    static_assert(requires(Device& device) { device.endFrameCommandBuffer(static_cast<ICommandBuffer*>(nullptr)); });
    static_assert(requires(Device& device) {
        { device.sealFrame(0u, static_cast<ICommandBuffer*>(nullptr)) } -> std::same_as<RecordedFrame>;
    });
    // The surface is named by id: filing this frame's present target under the
    // window's identity (not its address) is what keeps a reopened window out of
    // the previous window's imported images.
    static_assert(requires(Device& device, SurfaceId id, IRenderSurfaceContext& surface) {
        device.acquireSurfacePresentation(id, surface);
    });
    static_assert(requires(Device& device) { device.getLiveSubmission(0u); });
    static_assert(!PublishesFamilyResults<RenderDeviceState>,
                  "publishing is the family-recording step's own business, not a host step");
    static_assert(!LooksUpAPresentTargetWithoutBuildingOne<RenderDeviceState>,
                  "looking a present target up without being willing to build one is not a host step");
}

/// The gate itself: a plan with no acquired present opens no recording at all.
/// The host's `submitRecordedFrame` submits an empty frame in that case, so the
/// renderer must not have left a half-open submission behind for that flight.
TEST(RuntimeRenderContextTest, APlanWithoutAnAcquiredPresentOpensNoRecording)
{
    RenderDeviceState    device;
    RuntimeRenderContext context{device};

    const RecordedFrame recorded = context.record(RenderFramePlan{}, nullptr);

    EXPECT_FALSE(recorded.valid());
    EXPECT_EQ(recorded.commandBuffer, nullptr);
    EXPECT_EQ(device.getLiveSubmission(0), nullptr);
}

} // namespace
} // namespace ya
