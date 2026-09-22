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
    /// The strategy identity lives with the settings value type, so publishing
    /// "which pipeline is active" and "what settings that pipeline has" speak
    /// one vocabulary instead of two enums that mean the same thing.
    using ERenderPipeline = ERenderPipelineKind;

    struct InitDesc
    {
        IRender*                      render                = nullptr;
        IRenderRuntimeHostServices*   hostServices          = nullptr;
        RenderSharedResourceProvider* sharedResourceProvider = nullptr;
        /// Debug overlay sink, injected into the pipelines that draw it.
        DebugRenderSystem*            debugRenderSystem     = nullptr;
        /// Initial view extent in pixels, used for the first pipeline build
        /// only. Not swapchain extent. The first frame's View rect replaces it
        /// (see `applyPendingChanges`), so this is a seed for "what size is a
        /// pipeline before any View has declared one", not a viewport fact.
        int                           initialViewWidth      = 0;
        int                           initialViewHeight     = 0;
    };

    void init(const InitDesc& desc);
    void shutdown();

    /// Applied at frame start, in this order: pending pipeline switch/reload,
    /// the queued render-target format commands, and the view rect.
    ///
    /// `viewRect` is the frame's View rect -- the plan's display root -- as an
    /// INPUT. A pipeline built or rebuilt inside this call is sized from it, and
    /// a pipeline whose rect differs is resized. Holding the rect here instead
    /// (as the renderer used to) meant two opinions about a View's geometry, and
    /// the app had to push the same rect at the renderer every tick to keep them
    /// in step. A degenerate rect is "this frame declared no View": the last
    /// applied rect stands, because a pipeline keeps rendering its last geometry
    /// and a freshly built one must not come up at the init seed.
    ///
    /// Returns true when a pipeline was (re)built, which is also when the rect
    /// must be re-applied even if it did not change.
    bool applyPendingChanges(Rect2D viewRect);

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
    void initForwardPipeline(int viewWidth, int viewHeight);
    void initDeferredPipeline(int viewWidth, int viewHeight);
    void shutdownActivePipeline();
    /// Returns true when a switch/reload rebuilt the active pipeline.
    bool applyPendingRenderPipelineSwitch();
    void applyPendingRenderTargetFormatCommands();

    IRender*                         _render                = nullptr;
    IRenderRuntimeHostServices*      _hostServices          = nullptr;
    RenderSharedResourceProvider*    _sharedResourceProvider = nullptr;
    DebugRenderSystem*               _debugRenderSystem      = nullptr;
    int                              _initialViewWidth      = 0;
    int                              _initialViewHeight     = 0;
    /// This coordinator's own applied state: the rect its current pipelines were
    /// last sized to. Not a copy of a View's declaration -- the View's rect
    /// arrives as an argument on every call.
    Rect2D                           _appliedViewRect{};

    ERenderPipeline _renderPipeline          = ERenderPipeline::Deferred;
    ERenderPipeline _pendingRenderPipeline   = ERenderPipeline::Deferred;
    bool            _pendingActivePipelineReload = false;

    stdptr<ForwardRenderPipeline>  _forwardPipeline  = nullptr;
    stdptr<DeferredRenderPipeline> _deferredPipeline = nullptr;

    std::vector<RenderTargetFormatCommand> _pendingRenderTargetFormatCommands;
};

} // namespace ya
