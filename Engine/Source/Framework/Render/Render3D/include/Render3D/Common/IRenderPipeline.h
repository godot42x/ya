#pragma once

#include "RHI/RenderDefines.h"
#include "Render3D/Common/RenderFrameInputs.h"
#include "Render3D/Common/ShadowSettings.h"
#include "Render3D/Common/RenderOverlay.h"
#include "Render3D/Common/RenderTargetCatalog.h"

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

struct IRenderPipelineExecution
{
    virtual ~IRenderPipelineExecution() = default;

    virtual void onViewResized(Rect2D rect) = 0;
    /// Compile and record one Scene family graph; return each View's output.
    /// Product recording does not use tick/beginTick/getCurrent.
    virtual ViewFamilyRenderResult recordFamily(const ViewFamilyRecordContext& ctx) = 0;

    [[nodiscard]] virtual Extent2D   getViewExtent() const          = 0;
    [[nodiscard]] virtual EFormat::T getViewColorFormat() const     = 0;
    [[nodiscard]] virtual EFormat::T getViewDepthFormat() const     = 0;
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
    [[nodiscard]] virtual std::shared_ptr<RenderTexture> getViewDepthImageShared() const = 0;
    /// R32_UINT viewport target holding per-pixel entity ids (editor picking).
    [[nodiscard]] virtual std::shared_ptr<RenderTexture> getEntityIdImageShared() const { return nullptr; }
    [[nodiscard]] virtual std::shared_ptr<ImageResource> getShadowDirectionalDepthResource() const = 0;
    [[nodiscard]] virtual std::shared_ptr<ImageResource> getShadowPointFaceDepthResource(uint32_t pointLightIndex, uint32_t faceIndex) const = 0;
    /// Whether the view's postprocessing GRADES the image. Not whether the
    /// postprocess pass runs: the finalize pass always runs, because it is what
    /// encodes the linear view color into a display image.
    [[nodiscard]] virtual bool isGradingEnabled() const = 0;
    /// Color format of the postprocess output image (stable pipeline config;
    /// queryable before the world graph creates the actual image).
    [[nodiscard]] virtual EFormat::T getPostprocessColorFormat() const = 0;
};

struct IRenderPipeline : IRenderPipelineExecution,
                         IRenderPipelineRenderTargets,
                         IRenderPipelineDebugOutputs
{
    ~IRenderPipeline() override = default;
};

/// Coordinator-facing name for the family graph renderer. Concrete
/// Forward/Deferred pipelines keep their pass types.
using ISceneViewFamilyRenderer = IRenderPipeline;

} // namespace ya
