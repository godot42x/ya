#pragma once

#include "RHI/RenderDefines.h"
#include "AppServices/ShadowSettings.h"
#include "Render3D/Common/RenderOverlay.h"
#include "Render3D/Common/RenderTargetCatalog.h"

#include <functional>
#include <glm/glm.hpp>
#include <memory>

#include <cstdint>

namespace ya
{

struct ICommandBuffer;
struct IImageView;
struct IImage;
struct RenderImage;
struct Texture;
struct RenderFrameData;

struct RenderPipelineFrameContext
{
    uint32_t         flightIndex              = 0;
    ICommandBuffer*  cmdBuf                   = nullptr;
    float            deltaTime                = 0.0f;
    glm::mat4        view                     = glm::mat4(1.0f);
    glm::mat4        projection               = glm::mat4(1.0f);
    glm::vec3        cameraPos                = glm::vec3(0.0f);
    Rect2D           viewportRect             = {};
    float            viewportFrameBufferScale = 1.0f;
    RenderFrameData* frameData                = nullptr;
    const ShadowSettings* shadowSettings      = nullptr;
    std::shared_ptr<const RenderViewportOverlaySnapshot> viewportOverlaySnapshot = nullptr;
};

struct IRenderPipelineExecution
{
    virtual ~IRenderPipelineExecution() = default;

    virtual void onViewportResized(Rect2D rect) = 0;
    virtual void tick(const RenderPipelineFrameContext& frame) = 0;

    [[nodiscard]] virtual Extent2D   getViewportExtent() const          = 0;
    [[nodiscard]] virtual EFormat::T getViewportColorFormat() const     = 0;
    [[nodiscard]] virtual EFormat::T getViewportDepthFormat() const     = 0;
};

struct IRenderPipelineRenderTargets
{
    virtual ~IRenderPipelineRenderTargets() = default;

    virtual void appendRenderTargetEntries(RenderTargetCatalog& catalog) const {}
    virtual bool setRenderTargetDepthFormat(RenderTargetCatalog::Entry::EOwner owner, EFormat::T format) { return false; }
    virtual bool setRenderTargetColorFormat(RenderTargetCatalog::Entry::EOwner owner, uint32_t attachmentIndex, EFormat::T format) { return false; }
};

struct IRenderPipelineDebugOutputs
{
    virtual ~IRenderPipelineDebugOutputs() = default;

    [[nodiscard]] virtual bool           isShadowMappingEnabled() const = 0;
    [[nodiscard]] virtual std::shared_ptr<IImage> getShadowDepthImage() const = 0;
    [[nodiscard]] virtual std::shared_ptr<RenderImage> getViewportDepthImageShared() const = 0;
    /// R32_UINT viewport target holding per-pixel entity ids (editor picking).
    [[nodiscard]] virtual std::shared_ptr<RenderImage> getEntityIdImageShared() const { return nullptr; }
    [[nodiscard]] virtual IImageView*    getShadowDirectionalDepthIV() const = 0;
    [[nodiscard]] virtual IImageView*    getShadowPointFaceDepthIV(uint32_t pointLightIndex, uint32_t faceIndex) const = 0;
    [[nodiscard]] virtual bool     isPostprocessingEnabled() const     = 0;
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

} // namespace ya
