#pragma once

#include "RHI/RenderDefines.h"
#include "Render3D/Common/RenderFrameInputs.h"
#include "Render3D/Common/RenderPipelineSettings.h"
#include "Render3D/Common/ShadowSettings.h"
#include "Render3D/Common/RenderOverlay.h"
#include "Render3D/Common/RenderTargetCatalog.h"
#include "Render3D/Common/SurfaceImage.h"

#include <functional>
#include <glm/glm.hpp>
#include <memory>

#include <cstdint>

namespace ya
{

struct ICommandBuffer;
struct ImageResource;
struct RenderTexture;
struct Texture;
struct RenderFrameData;
struct ViewTargetRequest;

struct IRenderPipelineExecution
{
    virtual ~IRenderPipelineExecution() = default;

    /// This strategy's identity: Forward or Deferred. Answering it is what keeps
    /// a caller from downcasting to find out which strategy it holds.
    [[nodiscard]] virtual ERenderPipelineKind kind() const = 0;

    /// Compile and record one Scene family graph; return each View's output.
    /// Product recording does not use tick/beginTick/getCurrent.
    virtual ViewFamilyRenderResult recordFamily(const ViewFamilyRecordContext& ctx) = 0;
    /// Submission-scoped warmup: PSO beginFrame and the state that must land
    /// before any family records. Called once per submission, never per View.
    virtual void beginSubmission() = 0;
    virtual void appendTargetRequests(
        const SceneRenderPlan& plan,
        std::vector<ViewTargetRequest>& out) const = 0;

    [[nodiscard]] virtual EFormat::T getViewColorFormat() const     = 0;
    [[nodiscard]] virtual EFormat::T getViewDepthFormat() const     = 0;

};

struct IRenderPipelineSettings
{
    virtual ~IRenderPipelineSettings() = default;

    /// The settings this strategy would apply right now: the ones a request put
    /// in flight if there is one, otherwise the applied ones. Reading and writing
    /// settings through one value is how "what can be configured" stops being a
    /// question about the concrete class.
    [[nodiscard]] virtual RenderPipelineSettings resolveSettings() const = 0;
    virtual void requestSettings(const RenderPipelineSettings& settings) = 0;
};

struct IRenderPipelineRenderTargets
{
    virtual ~IRenderPipelineRenderTargets() = default;

    virtual void appendRenderTargetEntries([[maybe_unused]] RenderTargetCatalog& catalog) const {}
    virtual bool setRenderTargetDepthFormat([[maybe_unused]] RenderTargetCatalog::Entry::EOwner owner, [[maybe_unused]] EFormat::T format) { return false; }
    virtual bool setRenderTargetColorFormat([[maybe_unused]] RenderTargetCatalog::Entry::EOwner owner, [[maybe_unused]] uint32_t attachmentIndex, [[maybe_unused]] EFormat::T format) { return false; }
};

struct IRenderPipelineDebugOutputs
{
    virtual ~IRenderPipelineDebugOutputs() = default;

    [[nodiscard]] virtual bool isShadowMappingEnabled() const = 0;
    [[nodiscard]] virtual std::shared_ptr<ImageResource> getShadowDirectionalDepthResource() const = 0;
    [[nodiscard]] virtual std::shared_ptr<ImageResource> getShadowPointFaceDepthResource(uint32_t pointLightIndex, uint32_t faceIndex) const = 0;
    /// Whether the view's postprocessing GRADES the image. Not whether the
    /// postprocess pass runs: the finalize pass always runs, because it is what
    /// encodes the linear view color into a display image.
    [[nodiscard]] virtual bool isGradingEnabled() const = 0;
    /// Color format of the postprocess output image (stable pipeline config;
    /// queryable before the world graph creates the actual image).
    [[nodiscard]] virtual EFormat::T getPostprocessColorFormat() const = 0;
    /// Which transfer function this pipeline's display image carries. The color
    /// format says how many bits and which channels; this says what the values
    /// mean, which no format can express. The surface pass checks it against the
    /// surface before writing (see `findSurfaceImageMismatch`).
    [[nodiscard]] virtual EImageEncoding getDisplayImageEncoding() const = 0;
};

struct IRenderPipeline : IRenderPipelineExecution,
                         IRenderPipelineSettings,
                         IRenderPipelineRenderTargets,
                         IRenderPipelineDebugOutputs
{
    ~IRenderPipeline() override = default;
};

/// Coordinator-facing name for the family graph renderer. Concrete
/// Forward/Deferred pipelines keep their pass types.
using ISceneViewFamilyRenderer = IRenderPipeline;

} // namespace ya
