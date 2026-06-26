#pragma once

#include "Core/Base.h"
#include "Render/Core/Texture.h"
#include "Render/Pipelines/BasicPostprocessing.h"
#include "Render/Pipelines/BloomPostprocessing.h"
#include "Render/Render.h"
#include "Render/RenderDefines.h"
#include "Runtime/App/Common/PostProcessingState.h"


namespace ya
{

struct PostProcessingStage
{
    struct InitDesc
    {
        IRender*   render      = nullptr;
        EFormat::T colorFormat = EFormat::R8G8B8A8_UNORM;
        uint32_t   width       = 0;
        uint32_t   height      = 0;
    };

    IRender*                    _render             = nullptr;
    EFormat::T                  _colorFormat        = EFormat::R8G8B8A8_UNORM;
    bool                        bEnabled            = true;
    PostProcessingState         _state              = {};
    stdptr<BloomPostprocessing> _bloomProcessor     = nullptr;
    stdptr<BasicPostprocessing> _postProcessor      = nullptr;
    stdptr<Texture>             _bloomExtractTexture = nullptr;
    stdptr<Texture>             _bloomBlurPingTexture = nullptr;
    stdptr<Texture>             _bloomBlurPongTexture = nullptr;
    stdptr<Texture>             _bloomCompositeTexture = nullptr;
    stdptr<Texture>             _postprocessTexture = nullptr;

    void     init(const InitDesc& desc);
    void     shutdown();
    void     beginFrame();
    void     renderGUI();
    Texture* execute(ICommandBuffer* cmdBuf,
                     Texture*        inputTexture,
                     glm::vec2       viewportExtent,
                     FrameContext*   ctx);
    void     onViewportResized(Extent2D newExtent);

    [[nodiscard]] bool                       isEnabled() const { return bEnabled; }
    [[nodiscard]] Texture*                   getOutputTexture() const { return _postprocessTexture.get(); }
    [[nodiscard]] Texture*                   getBloomExtractTexture() const { return _bloomExtractTexture.get(); }
    [[nodiscard]] Texture*                   getBloomBlurTexture() const { return _bloomBlurPongTexture ? _bloomBlurPongTexture.get() : _bloomBlurPingTexture.get(); }
    [[nodiscard]] Texture*                   getBloomCompositeTexture() const { return _bloomCompositeTexture.get(); }
    [[nodiscard]] PostProcessingState&       getState() { return _state; }
    [[nodiscard]] const PostProcessingState& getState() const { return _state; }

  private:
    void recreateOutputTexture(Extent2D extent);
    void recreateBloomTextures(Extent2D extent);
};

} // namespace ya
