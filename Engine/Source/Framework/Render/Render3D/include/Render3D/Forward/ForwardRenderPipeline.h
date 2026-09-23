#pragma once

#include "Core/Base.h"
#include "Render3D/Forward/ForwardViewStage.h"
#include "Render3D/Forward/ForwardFrameGraphOrchestrator.h"
#include "Render3D/Forward/ForwardFrameResourceSet.h"
#include "Graph/RenderGraphExecutor.h"
#include "RHI/Core/RenderAttachmentFormats.h"
#include "RHI/Core/RenderTargetCreateInfo.h"
#include "RHI/Render.h"
#include "Render3D/RenderFrameData.h"
#include "Render3D/Common/IRenderPipeline.h"
#include "Render3D/Common/PostProcessingStage.h"
#include "Render3D/Common/EntityIdPass.h"
#include "Render3D/Common/Shadow/Common/ShadowMapResources.h"
#include "Render3D/Common/Shadow/Common/ShadowRuntimeState.h"
#include "Render3D/Common/Shadow/ShadowStage.h"


#include <array>
#include <glm/glm.hpp>
#include <optional>
#include <vector>

namespace ya
{

enum class EForwardPendingResourceRefresh : uint32_t
{
    None             = 0,
    ShadowResources  = 1 << 1,
    AttachmentFormat = 1 << 2,
};

struct RenderTexture;

struct Texture;
struct Sampler;

struct YA_RENDER_3D_API ForwardRenderPipeline : public IRenderPipeline
{
    static constexpr auto VIEWPORT_COLOR_FORMAT              = EFormat::R16G16B16A16_SFLOAT;
    static constexpr auto POSTPROCESS_COLOR_FORMAT           = EFormat::R8G8B8A8_UNORM;
    static constexpr auto DEPTH_FORMAT                       = EFormat::D32_SFLOAT_S8_UINT;
    static constexpr auto SHADOW_MAPPING_DEPTH_BUFFER_FORMAT = EFormat::D32_SFLOAT;

    struct InitDesc
    {
        IRender* render  = nullptr;
        int      viewWidth = 0;
        int      viewHeight = 0;
        ShadowSettings* shadowSettings = nullptr;
    };

    Deleter _deleter;

    IRender*                 _render          = nullptr;
    ShadowSettings*          _shadowSettings  = nullptr;

    stdptr<IDescriptorPool> _descriptorPool = nullptr;

    // Shadow resources (owned here, shared to stages)
    stdptr<IDescriptorSetLayout> depthBufferDSL      = nullptr;
    DescriptorSetHandle          depthBufferShadowDS = nullptr;
    ShadowMapResources           _shadowResources;
    EFormat::T                   _shadowDepthFormat = SHADOW_MAPPING_DEPTH_BUFFER_FORMAT;

    // ── Render stages ─────────────────────────────────────────────
    stdptr<ShadowStage>          _shadowStage;
    stdptr<ForwardViewStage> _viewStage;
    PostProcessingStage          _postProcessStage;
    ForwardFrameGraphOrchestrator _frameGraphOrchestrator{};
    std::unique_ptr<RenderGraphExecutor> _graphExecutor;
    stdptr<ForwardFrameResourceSet> _frameResources;
    RGTopologyDescription        _lastFrameGraphTopology{};

    bool                    bMSAA                    = false;

    uint32_t      _pendingResourceRefreshMask = 0;
    /// The pipeline's durable view-target configuration: the formats it builds
    /// View pipelines with, plus the extent a pipeline is seeded at before any
    /// View has declared one. A View's own extent is not here -- it is part of
    /// that View's resource key.
    RenderTargetCreateInfo _viewRTSpec{};
    RenderAttachmentFormats _viewFormats{};
    EntityIdPass     _entityIdPass{};
    ShadowSettings _frameShadowSettings = ShadowSettings::fromQuality(EShadowQuality::Off);
    std::optional<PostProcessingState> _pendingPostProcessSettings;

    void init(const InitDesc& desc);
    ViewFamilyRenderResult recordFamily(const ViewFamilyRecordContext& ctx) override;
    void appendTargetRequests(const SceneRenderPlan& plan, std::vector<ViewTargetRequest>& out) const override;
    void shutdown();

    bool setRenderTargetColorFormat(RenderTargetCatalog::Entry::EOwner owner,
                                    uint32_t                                 attachmentIndex,
                                    EFormat::T                               format) override;
    bool setRenderTargetDepthFormat(RenderTargetCatalog::Entry::EOwner owner,
                                    EFormat::T                               format) override;

    [[nodiscard]] ERenderPipelineKind kind() const override { return ERenderPipelineKind::Forward; }
    [[nodiscard]] EFormat::T     getViewColorFormat() const override;
    [[nodiscard]] EFormat::T     getViewDepthFormat() const override;
    [[nodiscard]] const RGTopologyDescription& getLastFrameGraphTopology() const override
    {
        return _lastFrameGraphTopology;
    }
    void appendRenderTargetEntries(RenderTargetCatalog& catalog) const override;

    [[nodiscard]] bool           isShadowMappingEnabled() const override;
    [[nodiscard]] std::shared_ptr<ImageResource> getShadowDirectionalDepthResource() const override;
    [[nodiscard]] std::shared_ptr<ImageResource> getShadowPointFaceDepthResource(uint32_t pointLightIndex, uint32_t faceIndex) const override;
    [[nodiscard]] bool           isGradingEnabled() const override { return _postProcessStage.isGradingEnabled(); }
    [[nodiscard]] EFormat::T     getPostprocessColorFormat() const override { return POSTPROCESS_COLOR_FORMAT; }
    /// The finalize pass's gamma stage is what makes a linear render target a
    /// display image, so the encoding of this pipeline's display image is
    /// exactly that switch. The looks it grades (tonemap, bloom, grain) do not
    /// matter here: they are in the same image either way, and the surface takes
    /// the image as it found it.
    [[nodiscard]] EImageEncoding getDisplayImageEncoding() const override
    {
        return getPostProcessSettings().bEnableGammaCorrection ? EImageEncoding::DisplayEncoded
                                                               : EImageEncoding::Linear;
    }
    [[nodiscard]] ShadowSettings getCurrentShadowSettings() const;
    void                         requestShadowSettings(const ShadowSettings& shadowSettings);
    [[nodiscard]] PostProcessingState getPostProcessSettings() const;
    [[nodiscard]] PostProcessingState resolvePostProcessSettings() const;
    void                         requestPostProcessSettings(const PostProcessingState& settings);

    /// The forward strategy's settings as one value. It reads `shadow` and
    /// `postProcessing` and carries the strategy-specific block unchanged: those
    /// switches describe a deferred pipeline, which `kind` states, so nothing
    /// here applies them silently.
    [[nodiscard]] RenderPipelineSettings resolveSettings() const override;
    void requestSettings(const RenderPipelineSettings& settings) override;

  private:
    void               initViewResources(const InitDesc& desc);
    void               initPostProcessResources(const InitDesc& desc);
    void               initShadowResources();
    void               initStageResources();
    [[nodiscard]] bool shouldSkipView(const RenderPipelineFrameContext& frame) const;
    void               beginViewRecording(const RenderPipelineFrameContext& frame, RenderStageContext& stageCtx);
    void               markPendingResourceRefresh(EForwardPendingResourceRefresh refresh);
    [[nodiscard]] bool hasPendingResourceRefresh(EForwardPendingResourceRefresh refresh) const;
    void               clearPendingResourceRefresh(EForwardPendingResourceRefresh refresh);
    void               requestShadowResourceRefresh();
    void               applyPendingResourceRefreshes();
    void               syncFrameSettings(const RenderPipelineFrameContext& frame);
    void               refreshViewSnapshot();
    void               refreshViewStageState();
    void               refreshShadowStageState();
    bool               appendViewportPassGraph(RenderGraph& graph,
                                               const RenderPipelineFrameContext& frame,
                                               RenderStageContext&             stageCtx,
                                               const ShadowPreparedView&       shadowPrepared,
                                               FrameContext&                    postContext,
                                               ForwardViewStage::PassContext& viewPassContext,
                                               const ForwardFrameResourceSet::Binding& frameBinding,
                                               ForwardFrameResourceSet::ViewResources* viewResources,
                                               const ViewTargetLease& targets,
                                               std::optional<RGPassHandle> familyPredecessor);
    [[nodiscard]] RenderViewOutput collectViewOutput(const RenderGraphExecutionResult& result,
                                                     const SceneViewTask* task,
                                                     uint64_t viewId,
                                                     Extent2D viewExtent) const;
    void               syncShadowSettings();
    void               captureShadowSettings(const RenderPipelineFrameContext& frame);
    [[nodiscard]] ShadowSettings currentShadowSettings() const;
    [[nodiscard]] ShadowRuntimeState buildShadowState() const;
    /// Returns the token the View's graph build must hand back to the shadow
    /// stage. An invalid token means this View appends no shadow passes.
    [[nodiscard]] ShadowPreparedView executeShadowPass(const RenderPipelineFrameContext& frame,
                                                       RenderStageContext&               stageCtx);
    void               rebuildShadowViews();
    void               applyShadowSettings(const ShadowSettings& shadowSettings);
    void               applyPendingPostProcessSettings();
};

} // namespace ya
