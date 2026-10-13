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
#include <vector>

#include <cstdint>

namespace ya
{

struct ICommandBuffer;
struct ImageResource;
struct RenderTexture;
struct Texture;
struct RenderFrameData;
struct ViewTargetRequest;
struct PreparedPipelineFrame;

/// Outcome of a pipeline's safe-point composition step.
enum class EFrameComposition
{
    /// Composition resolved and the View target requests it requires produced.
    Resolved,
    /// Composition resolution failed; the frame must not record.
    Failed,
};

struct IRenderPipelineExecution
{
    virtual ~IRenderPipelineExecution() = default;

    /// This strategy's identity: Forward or Deferred. Answering it is what keeps
    /// a caller from downcasting to find out which strategy it holds.
    [[nodiscard]] virtual ERenderPipelineKind kind() const = 0;

    /// Compile and record one Scene family graph; return each View's output.
    /// Product recording does not use tick/beginTick/getCurrent.
    virtual ViewFamilyRenderResult recordFamily(const ViewFamilyRecordContext& ctx) = 0;
    /// Safe point, step 1 of 2: apply this frame's queued settings so the
    /// composition resolves from frozen values. Runs before
    /// `resolveFrameComposition`, in the same safe point (see design §6), and is
    /// the only place settings change for a frame.
    virtual void applyFrameSettings(const RenderFramePlan& plan) = 0;
    /// Safe point, step 2 of 2: prepare the persistent device resources the
    /// resolved composition needs (shadow images, format variants) and record
    /// what it decided for the submission's warmup. Runs after the composition
    /// resolved and before any command buffer exists, so recording never has to
    /// mutate a persistent resource or re-decide enablement.
    virtual void prepareFrameDevices(const PreparedPipelineFrame& prepared) = 0;
    /// Submission-scoped warmup: PSO beginFrame and the state that must land
    /// before any family records. Called once per submission, never per View.
    /// The frame's resolved composition arrives as an argument: which PSOs need
    /// warming follows from what the composition turned on, not from a second
    /// enablement flag kept beside it.
    virtual void beginSubmission(const PreparedPipelineFrame& prepared) = 0;
    /// Safe-point composition: resolve this frame's typed composition, fill
    /// `prepared` for recording, and produce the View target requests it
    /// requires. Both world pipelines resolve one, so allocation and the passes
    /// that consume it come from the same resolved values.
    [[nodiscard]] virtual EFrameComposition resolveFrameComposition(
        const RenderFramePlan& plan,
        PreparedPipelineFrame& prepared,
        std::vector<ViewTargetRequest>& out) = 0;

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
