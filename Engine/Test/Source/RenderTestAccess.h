#pragma once

#include "Render3D/Common/RenderFrameInputs.h"
#include "Render3D/Common/RenderFeatures.h"
#include "Render3D/Common/RenderViewOutput.h"
#include "Render3D/Forward/ForwardRenderPipeline.h"
#include "Render3D/RenderDeviceState.h"
#include "Render3D/Services/PipelineCoordinator.h"

#include <memory>

namespace ya
{

/// Publishes into a pipeline the test owns without standing up a backend. The
/// pipeline's own publication is what a real `recordFamily` does; the test names
/// the View directly so a case can put two Views in the table and then watch
/// which of them a tick's declarations keep.
class ForwardRenderPipelineTestAccess
{
  public:
    static void publishViewResources(ForwardRenderPipeline&  pipeline,
                                     const RenderViewOutput& output,
                                     Extent2D                extent,
                                     FRenderFeatureMask      features)
    {
        pipeline.publishViewResources(output, extent, features);
    }

    static void reconcilePublishedViews(ForwardRenderPipeline& pipeline, const SceneRenderPlan& plan)
    {
        pipeline.reconcilePublishedViews(plan);
    }
};

/// Installs a pipeline the test owns as the coordinator's active strategy.
/// Building one goes through `initForwardPipeline` and needs a real backend, so
/// this is the seam instead of the coordinator growing a public
/// install-the-strategy entry point just for a test.
class PipelineCoordinatorTestAccess
{
  public:
    static void installForwardPipeline(PipelineCoordinator&                   coordinator,
                                       std::shared_ptr<ForwardRenderPipeline> pipeline)
    {
        coordinator._forwardPipeline       = std::move(pipeline);
        coordinator._renderPipeline        = PipelineCoordinator::ERenderPipeline::Forward;
        coordinator._pendingRenderPipeline = PipelineCoordinator::ERenderPipeline::Forward;
    }
};

/// Drives the device with a pipeline the test published into. A case that wants
/// to prove the eviction a tick's declarations drive has to reach the device
/// with a pipeline installed, and building one is a device-lifetime action a
/// real backend performs, so the test names itself instead of the header
/// growing an install-the-strategy entry point. Same shape as the two pipelines'
/// `ForwardRenderPipelineTestAccess` / `DeferredRenderPipelineTestAccess`.
///
/// The recording steps themselves (`prepareFrameRecord`, `beginFrameCommandBuffer`,
/// ...) are public: they are the steps an application's recording order calls, so
/// a case reaching them needs this class only for the installed strategy.
class RenderDeviceStateTestAccess
{
  public:
    static void installActivePipeline(RenderDeviceState&                     device,
                                      std::shared_ptr<ForwardRenderPipeline> pipeline)
    {
        PipelineCoordinatorTestAccess::installForwardPipeline(device._pipelineCoordinator, std::move(pipeline));
    }
};

} // namespace ya
