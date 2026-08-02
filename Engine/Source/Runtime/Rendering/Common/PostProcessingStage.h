#pragma once

#include "Core/Base.h"
#include "Render/Core/RenderGraphExecutor.h"
#include "Render/Core/RenderImage.h"
#include "Render/Core/Texture.h"
#include "Render/Pipelines/BasicPostprocessing.h"
#include "Render/Pipelines/BloomPostprocessing.h"
#include "Render/Render.h"
#include "Render/RenderDefines.h"
#include "Runtime/Rendering/Common/PostProcessingState.h"


namespace ya
{

struct PostProcessingStage
{
    struct GraphBuildResult
    {
        RGTextureHandle input{};
        RGTextureHandle output{};
    };

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
    std::unique_ptr<RenderGraphExecutor> _graphExecutor;
    GraphBuildResult            _preparedGraphResources{};
    stdptr<RenderImage>         _preparedOutputImage = nullptr;

    void     init(const InitDesc& desc);
    void     shutdown();
    void     beginFrame();
    void     setEnabled(bool enabled) { bEnabled = enabled; }
    void     setBloomEnabled(bool enabled) { _state.bEnableBloom = enabled; }
    void     setToneMappingEnabled(bool enabled) { _state.bEnableToneMapping = enabled; }
    void     setToneMappingCurve(PostProcessingState::EToneMappingCurve curve) { _state.toneMappingCurve = curve; }
    RGTextureHandle appendBloomGraphPasses(RenderGraph&   graph,
                                           RGTextureHandle input,
                                           Extent2D        inputExtent,
                                           FrameContext*   ctx);
    RGTextureHandle appendFinalizeGraphPasses(RenderGraph&   graph,
                                              RGTextureHandle input,
                                              Extent2D        inputExtent,
                                              FrameContext*   ctx);
    RGTextureHandle appendGraphPasses(RenderGraph& graph,
                                      Texture*      inputTexture,
                                      glm::vec2     viewportExtent,
                                      FrameContext* ctx);
    RGTextureHandle appendGraphPasses(RenderGraph& graph,
                                      RenderImage*  inputImage,
                                      glm::vec2     viewportExtent,
                                      FrameContext* ctx);
    RGTextureHandle appendGraphPasses(RenderGraph& graph,
                                      RGTextureHandle input,
                                      Extent2D        inputExtent,
                                      FrameContext*   ctx);
    void     resolvePreparedResources(const RenderGraphResourceRegistry& registry);
    void     clearPreparedResources();
    RenderImage* execute(ICommandBuffer* cmdBuf,
                         Texture*        inputTexture,
                         glm::vec2       viewportExtent,
                         FrameContext*   ctx);
    RenderImage* execute(ICommandBuffer* cmdBuf,
                         RenderImage*    inputImage,
                         glm::vec2       viewportExtent,
                         FrameContext*   ctx);

    [[nodiscard]] bool                       isEnabled() const { return bEnabled; }
    [[nodiscard]] stdptr<RenderImage>        getBloomExtractImageShared() const { return _bloomProcessor ? _bloomProcessor->getExtractImageShared() : nullptr; }
    [[nodiscard]] stdptr<RenderImage>        getBloomBlurImageShared() const { return _bloomProcessor ? _bloomProcessor->getBlurImageShared() : nullptr; }
    [[nodiscard]] stdptr<RenderImage>        getBloomCompositeImageShared() const { return _bloomProcessor ? _bloomProcessor->getCompositeImageShared() : nullptr; }
    [[nodiscard]] stdptr<RenderImage>        getPreparedOutputImageShared() const { return _preparedOutputImage; }
    [[nodiscard]] PostProcessingState&       getState() { return _state; }
    [[nodiscard]] const PostProcessingState& getState() const { return _state; }
};

} // namespace ya
