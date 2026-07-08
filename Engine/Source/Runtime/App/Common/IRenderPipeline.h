#pragma once

#include "Render/RenderDefines.h"

#include <cstdint>

namespace ya
{

struct ICommandBuffer;
struct IRenderTarget;
struct IImageView;
struct Texture;

struct IRenderPipeline
{
    virtual ~IRenderPipeline() = default;

    virtual void onViewportResized(Rect2D rect) = 0;

    virtual void renderGeneralSettingsGUI()     = 0;
    virtual void renderLightingSettingsGUI()    {}
    virtual void renderAOSettingsGUI()          {}
    virtual void renderShadowSettingsGUI()      = 0;
    virtual void renderPostProcessSettingsGUI() = 0;
    virtual void renderPerformanceGUI()         = 0;
    virtual void renderStageInternalsGUI()      = 0;

    [[nodiscard]] virtual bool           hasOpenViewportPass() const = 0;
    [[nodiscard]] virtual Extent2D       getViewportExtent() const   = 0;
    [[nodiscard]] virtual IRenderTarget* getViewportRT() const       = 0;
    [[nodiscard]] virtual Texture*       getViewportTexture() const  = 0;
    virtual void                         endViewportPass(ICommandBuffer* cmdBuf) = 0;

    [[nodiscard]] virtual bool           isShadowMappingEnabled() const = 0;
    [[nodiscard]] virtual IRenderTarget* getShadowDepthRT() const       = 0;
    [[nodiscard]] virtual IImageView*    getShadowDirectionalDepthIV() const = 0;
    [[nodiscard]] virtual IImageView*    getShadowPointFaceDepthIV(uint32_t pointLightIndex, uint32_t faceIndex) const = 0;

    [[nodiscard]] virtual Texture* getPostprocessOutputTexture() const = 0;
    [[nodiscard]] virtual Texture* getBloomExtractTexture() const      = 0;
    [[nodiscard]] virtual Texture* getBloomBlurTexture() const         = 0;
    [[nodiscard]] virtual Texture* getBloomCompositeTexture() const    = 0;
    [[nodiscard]] virtual bool     isPostprocessingEnabled() const     = 0;
};

} // namespace ya
