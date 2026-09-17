#pragma once

#include "Core/Base.h"
#include "Render3D/Common/RenderFrameInputs.h"

#include <memory>

namespace ya
{

struct ICommandBuffer;
struct RenderDeviceState;
struct RenderViewportOverlaySnapshot;

/// Consumes a sealed `RenderFramePlan`, acquires a submission, records Scene
/// family graphs, then View/UI/display compose. Does not own the backend and
/// does not locate an active Scene.
struct YA_RENDER_3D_API RenderFrameCoordinator
{
    explicit RenderFrameCoordinator(RenderDeviceState& device);

    /// Records graphics → UI → view compose → display compose. Caller must
    /// already have acquired `plan.present` and must `submitPresentFrame`
    /// with the returned command buffer (or an empty list if null).
    [[nodiscard]] ICommandBuffer* record(const RenderFramePlan& plan);

  private:
    RenderDeviceState* _device = nullptr;

    [[nodiscard]] bool validateSceneRenderInput(const RenderFramePlan& plan) const;
    void               recordViewFamilies(
                          const RenderFramePlan& plan,
                          ICommandBuffer* cmdBuf,
                          std::shared_ptr<RenderViewportOverlaySnapshot> overlaySnapshot);
};

} // namespace ya
