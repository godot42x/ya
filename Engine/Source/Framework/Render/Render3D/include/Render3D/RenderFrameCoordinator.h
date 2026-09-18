#pragma once

#include "Core/Base.h"
#include "Render3D/Common/RenderFrameInputs.h"
#include "Render3D/Common/RecordedFrame.h"

namespace ya
{

struct ICommandBuffer;
struct RenderDeviceState;
/// Consumes a sealed `RenderFramePlan`, acquires a submission, records Scene
/// family graphs, then View/UI/display compose. Does not own the backend and
/// does not locate an active Scene; every view binds the Scene on its own task.
/// `record` accepts only an `ExtractedSceneRender`, so a plan whose Scene
/// content was never extracted cannot be recorded.
struct YA_RENDER_3D_API RenderFrameCoordinator
{
    explicit RenderFrameCoordinator(RenderDeviceState& device);

    /// Records graphics → UI → view compose → display compose. Caller must
    /// already have acquired `plan.present`, and then submits what comes back:
    /// the recorded command buffer, or an empty frame when the result is
    /// invalid (see RecordedFrame).
    [[nodiscard]] RecordedFrame record(const RenderFramePlan& plan);

  private:
    RenderDeviceState* _device = nullptr;

    void recordViewFamilies(const RenderFramePlan& plan,
                            ICommandBuffer*        cmdBuf);
};

} // namespace ya
