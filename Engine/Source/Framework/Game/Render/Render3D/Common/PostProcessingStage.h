#pragma once

#include "Core/Base.h"
#include "RHI/Core/RenderImage.h"
#include "RHI/Core/Texture.h"
#include "Render3D/Pipelines/BasicPostprocessing.h"
#include "Render3D/Pipelines/BloomPostprocessing.h"
#include "RHI/Render.h"
#include "RHI/RenderDefines.h"
#include "Render3D/Common/PostProcessingState.h"

#include <string_view>


namespace ya
{

struct PostProcessingStage
{
    static constexpr std::string_view kOutputExportName = "Postprocessing.Output";

    struct FinalizePassParams
    {
        RGTextureHandle input{};
        RGTextureHandle output{};
        Extent2D        inputExtent{};
        bool            bOutputIsSRGB = false;
        FrameContext*   postContext    = nullptr;
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
    RGTextureHandle appendFinalizeGraphPasses(RenderGraph& graph, const FinalizePassParams& params);
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
    void     capturePreparedResources(const RenderGraphExecutionResult& result);
    void     clearPreparedResources();
    [[nodiscard]] bool                       isEnabled() const { return bEnabled; }
    [[nodiscard]] stdptr<RenderImage>        getBloomExtractImageShared() const { return _bloomProcessor ? _bloomProcessor->getExtractImageShared() : nullptr; }
    [[nodiscard]] stdptr<RenderImage>        getBloomBlurImageShared() const { return _bloomProcessor ? _bloomProcessor->getBlurImageShared() : nullptr; }
    [[nodiscard]] stdptr<RenderImage>        getBloomCompositeImageShared() const { return _bloomProcessor ? _bloomProcessor->getCompositeImageShared() : nullptr; }
    [[nodiscard]] stdptr<RenderImage>        getPreparedOutputImageShared() const { return _preparedOutputImage; }
    [[nodiscard]] PostProcessingState&       getState() { return _state; }
    [[nodiscard]] const PostProcessingState& getState() const { return _state; }
};

} // namespace ya
