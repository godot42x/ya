#pragma once

#include "Core/Base.h"

#include "Core/Common/Types.h"
#include "Render3D/Common/IRenderPipeline.h"
#include "Render3D/Common/RenderTargetCatalog.h"
#include "Render3D/Deferred/DeferredRenderPipeline.h"
#include "Render3D/Forward/ForwardRenderPipeline.h"

#include <functional>
#include <vector>

namespace ya
{

struct IRender;
struct IRenderRuntimeHostServices;
struct IRenderRuntimeServices;
struct RenderSharedResourceProvider;

/**
 * Owns runtime pipeline selection / switching / lifecycle.
 *
 * Coordination-only: it holds the two concrete pipelines and decides which is
 * active and when to switch or reload, but defines no rendering-strategy
 * interface of its own — both pipelines are used as-is through
 * `IRenderPipeline`, so Forward/Deferred remain separate strategies.
 */
struct YA_RENDER_3D_API PipelineCoordinator
{
    enum class ERenderPipeline
    {
        Forward,
        Deferred
    };

    struct InitDesc
    {
        IRender*                      render                = nullptr;
        IRenderRuntimeHostServices*   hostServices          = nullptr;
        RenderSharedResourceProvider* sharedResourceProvider = nullptr;
        /// Injected as `runtimeServices` into both pipelines.
        IRenderRuntimeServices*       runtimeServices       = nullptr;
        /// Invoked after a pipeline switch/reload so the owner can re-apply
        /// its current viewport rect to the freshly built pipeline.
        std::function<void()>         reapplyViewportSink;
    };

    void init(const InitDesc& desc);
    void shutdown();

    /// Applied at frame start: pending pipeline switch/reload first, then the
    /// queued render-target format commands (both act on the active pipeline).
    void applyPendingChanges();

    [[nodiscard]] IRenderPipeline* getActivePipeline() const;
    [[nodiscard]] ForwardRenderPipeline*  getSelectedForwardPipeline() const;
    [[nodiscard]] DeferredRenderPipeline* getSelectedDeferredPipeline() const;
    [[nodiscard]] ERenderPipeline getRenderPipeline() const { return _renderPipeline; }
    [[nodiscard]] ERenderPipeline getPendingRenderPipeline() const { return _pendingRenderPipeline; }
    void setPendingRenderPipeline(ERenderPipeline renderPipeline) { _pendingRenderPipeline = renderPipeline; }
    void requestActivePipelineReload() { _pendingActivePipelineReload = true; }
    [[nodiscard]] bool isDeferredPipelineActive() const { return _renderPipeline == ERenderPipeline::Deferred; }
    [[nodiscard]] bool hasAnyPipeline() const { return _forwardPipeline != nullptr || _deferredPipeline != nullptr; }
    [[nodiscard]] bool hasForwardPipeline() const { return _forwardPipeline != nullptr; }
    [[nodiscard]] bool hasDeferredPipeline() const { return _deferredPipeline != nullptr; }

    void requestRenderTargetFormat(const RenderTargetFormatCommand& command);

  private:
    void initActivePipeline();
    void initForwardPipeline(int windowWidth, int windowHeight);
    void initDeferredPipeline(int windowWidth, int windowHeight);
    void shutdownActivePipeline();
    void applyPendingRenderPipelineSwitch();
    void applyPendingRenderTargetFormatCommands();

    IRender*                         _render                = nullptr;
    IRenderRuntimeHostServices*      _hostServices          = nullptr;
    RenderSharedResourceProvider*    _sharedResourceProvider = nullptr;
    IRenderRuntimeServices*          _runtimeServices       = nullptr;
    std::function<void()>            _reapplyViewportSink;

    ERenderPipeline _renderPipeline          = ERenderPipeline::Deferred;
    ERenderPipeline _pendingRenderPipeline   = ERenderPipeline::Deferred;
    bool            _pendingActivePipelineReload = false;

    stdptr<ForwardRenderPipeline>  _forwardPipeline  = nullptr;
    stdptr<DeferredRenderPipeline> _deferredPipeline = nullptr;

    std::vector<RenderTargetFormatCommand> _pendingRenderTargetFormatCommands;
};

} // namespace ya