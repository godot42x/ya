#pragma once

#include "Render/RenderDefines.h"
#include "Render/Shadow/ShadowSettings.h"

#include <functional>
#include <glm/glm.hpp>

#include <cstdint>

namespace ya
{

struct ICommandBuffer;
struct IRenderTarget;
struct IImageView;
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
    std::function<void(ICommandBuffer*, Extent2D, const struct FrameContext&)> recordViewportOverlays;
};

struct IRenderPipelineExecution
{
    virtual ~IRenderPipelineExecution() = default;

    virtual void onViewportResized(Rect2D rect) = 0;
    virtual void tick(const RenderPipelineFrameContext& frame) = 0;

    [[nodiscard]] virtual Extent2D       getViewportExtent() const   = 0;
    [[nodiscard]] virtual IRenderTarget* getViewportRT() const       = 0;
    [[nodiscard]] virtual Texture*       getViewportTexture() const  = 0;
};

struct IRenderPipelineSettingsUI
{
    virtual ~IRenderPipelineSettingsUI() = default;

    virtual void renderGeneralSettingsGUI()     = 0;
    virtual void renderLightingSettingsGUI()    {}
    virtual void renderAOSettingsGUI()          {}
    virtual void renderShadowSettingsGUI()      = 0;
    virtual void renderPostProcessSettingsGUI() = 0;
};

struct IRenderPipelineDebugUI
{
    virtual ~IRenderPipelineDebugUI() = default;

    virtual void renderPerformanceGUI()    = 0;
    virtual void renderStageInternalsGUI() = 0;
};

struct IRenderPipelineDebugOutputs
{
    virtual ~IRenderPipelineDebugOutputs() = default;

    [[nodiscard]] virtual bool           isShadowMappingEnabled() const = 0;
    [[nodiscard]] virtual IRenderTarget* getShadowDepthRT() const       = 0;
    [[nodiscard]] virtual IImageView*    getShadowDirectionalDepthIV() const = 0;
    [[nodiscard]] virtual IImageView*    getShadowPointFaceDepthIV(uint32_t pointLightIndex, uint32_t faceIndex) const = 0;

    [[nodiscard]] virtual Texture* getPostprocessOutputTexture() const = 0;
    [[nodiscard]] virtual RenderImage* getBloomExtractImage() const    = 0;
    [[nodiscard]] virtual RenderImage* getBloomBlurImage() const       = 0;
    [[nodiscard]] virtual RenderImage* getBloomCompositeImage() const  = 0;
    [[nodiscard]] virtual bool     isPostprocessingEnabled() const     = 0;
};

struct IRenderPipeline : IRenderPipelineExecution,
                         IRenderPipelineSettingsUI,
                         IRenderPipelineDebugUI,
                         IRenderPipelineDebugOutputs
{
    ~IRenderPipeline() override = default;
};

} // namespace ya
