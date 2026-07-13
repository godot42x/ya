#pragma once

#include "Core/Base.h"
#include "Render/Core/RenderImage.h"
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
    stdptr<RenderImage>         _bloomExtractImage = nullptr;
    stdptr<RenderImage>         _bloomBlurPingImage = nullptr;
    stdptr<RenderImage>         _bloomBlurPongImage = nullptr;
    stdptr<RenderImage>         _bloomCompositeImage = nullptr;
    stdptr<RenderImage>         _postprocessOutputImage = nullptr;
    stdptr<Texture>             _postprocessOutputTextureCompat = nullptr;

    void     init(const InitDesc& desc);
    void     shutdown();
    void     beginFrame();
    void     resizeResources(Extent2D newExtent);
    void     renderGUI();
    void     renderSettingsGUI();
    void     renderTechnicalGUI();
    Texture* execute(ICommandBuffer* cmdBuf,
                     Texture*        inputTexture,
                     glm::vec2       viewportExtent,
                     FrameContext*   ctx);

    [[nodiscard]] bool                       isEnabled() const { return bEnabled; }
    [[nodiscard]] Texture*                   getOutputTexture() const { return _postprocessOutputTextureCompat.get(); }
    [[nodiscard]] RenderImage*               getOutputImage() const { return _postprocessOutputImage.get(); }
    [[nodiscard]] RenderImage*               getBloomExtractImage() const { return _bloomExtractImage.get(); }
    [[nodiscard]] RenderImage*               getBloomBlurImage() const { return _bloomBlurPongImage ? _bloomBlurPongImage.get() : _bloomBlurPingImage.get(); }
    [[nodiscard]] RenderImage*               getBloomCompositeImage() const { return _bloomCompositeImage.get(); }
    [[nodiscard]] PostProcessingState&       getState() { return _state; }
    [[nodiscard]] const PostProcessingState& getState() const { return _state; }

  private:
    void recreateOutputTexture(Extent2D extent);
    void recreateBloomTextures(Extent2D extent);
};

} // namespace ya
