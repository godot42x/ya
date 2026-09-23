#pragma once

#include "Core/Base.h"
#include "RHI/Core/RenderTexture.h"
#include "RHI/Core/Texture.h"
#include "Render3D/Pipelines/BasicPostprocessing.h"
#include "Render3D/Pipelines/BloomPostprocessing.h"
#include "RHI/Render.h"
#include "RHI/RenderDefines.h"
#include "Render3D/Common/PostProcessingState.h"
#include "Render3D/Common/ViewPassResources.h"

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
        uint64_t        viewId         = 0;
        ToneMapPassBindings toneMap{};
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
    /// Whether grading runs. NOT whether the pass runs: the finalize pass is
    /// what turns the renderer's linear color into a display image, so it runs
    /// every frame, and this flag only decides whether the look-changing stages
    /// (inversion / grayscale / kernel / tonemap / grain / bloom) apply.
    bool                        bGradingEnabled     = true;
    PostProcessingState         _state              = {};
    stdptr<BloomPostprocessing> _bloomProcessor     = nullptr;
    stdptr<BasicPostprocessing> _postProcessor      = nullptr;
    stdptr<RenderTexture>       _preparedOutputImage = nullptr;

    void     init(const InitDesc& desc);
    void     shutdown();
    void     beginFrame();
    void     setGradingEnabled(bool enabled) { bGradingEnabled = enabled; }
    void     setBloomEnabled(bool enabled) { _state.bEnableBloom = enabled; }
    void     setToneMappingEnabled(bool enabled) { _state.bEnableToneMapping = enabled; }
    void     setToneMappingCurve(PostProcessingState::EToneMappingCurve curve) { _state.toneMappingCurve = curve; }
    RGTextureHandle appendBloomGraphPasses(RenderGraph&   graph,
                                           RGTextureHandle input,
                                           Extent2D        inputExtent,
                                           FrameContext*   ctx,
                                           uint64_t        viewId = 0,
                                           const BloomPassBindings& bloom = {},
                                           RGTextureHandle bloomExtract = {},
                                           RGTextureHandle bloomBlur = {},
                                           RGTextureHandle bloomComposite = {});
    RGTextureHandle appendFinalizeGraphPasses(RenderGraph& graph, const FinalizePassParams& params);
    RGTextureHandle appendGraphPasses(RenderGraph& graph,
                                      Texture*      inputTexture,
                                      glm::vec2     viewExtent,
                                      FrameContext* ctx);
    RGTextureHandle appendGraphPasses(RenderGraph& graph,
                                      RenderTexture* inputImage,
                                      glm::vec2      viewExtent,
                                      FrameContext*  ctx);
    RGTextureHandle appendGraphPasses(RenderGraph& graph,
                                      RGTextureHandle input,
                                      Extent2D        inputExtent,
                                      FrameContext*   ctx);
    void     capturePreparedResources(const RenderGraphExecutionResult& result, uint64_t viewId = 0);
    void     clearPreparedResources();
    [[nodiscard]] bool                       isGradingEnabled() const { return bGradingEnabled; }
    [[nodiscard]] stdptr<RenderTexture>      getBloomExtractImageShared() const { return _bloomProcessor ? _bloomProcessor->getExtractImageShared() : nullptr; }
    [[nodiscard]] stdptr<RenderTexture>      getBloomBlurImageShared() const { return _bloomProcessor ? _bloomProcessor->getBlurImageShared() : nullptr; }
    [[nodiscard]] stdptr<RenderTexture>      getBloomCompositeImageShared() const { return _bloomProcessor ? _bloomProcessor->getCompositeImageShared() : nullptr; }
    [[nodiscard]] stdptr<RenderTexture>      getPreparedOutputImageShared() const { return _preparedOutputImage; }
    [[nodiscard]] PostProcessingState&       getState() { return _state; }
    [[nodiscard]] const PostProcessingState& getState() const { return _state; }
    [[nodiscard]] stdptr<IDescriptorSetLayout> getBloomExtractDSL() const
    {
        return _bloomProcessor ? _bloomProcessor->getExtractDSL() : nullptr;
    }
    [[nodiscard]] stdptr<IDescriptorSetLayout> getBloomBlurDSL() const
    {
        return _bloomProcessor ? _bloomProcessor->getBlurDSL() : nullptr;
    }
    [[nodiscard]] stdptr<IDescriptorSetLayout> getBloomCompositeDSL() const
    {
        return _bloomProcessor ? _bloomProcessor->getCompositeDSL() : nullptr;
    }
    [[nodiscard]] stdptr<IDescriptorSetLayout> getToneMapInputDSL() const
    {
        return _postProcessor ? _postProcessor->getInputDSL() : nullptr;
    }
};

} // namespace ya
