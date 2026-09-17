#pragma once

#include "Core/Base.h"
#include "RHI/Core/DescriptorSet.h"
#include "RHI/Core/Pipeline.h"
#include "Graph/RenderGraphExecutor.h"
#include "RHI/Core/RenderTexture.h"
#include "Render3D/Common/PostProcessingState.h"
#include "Render3D/Common/ViewPassResources.h"

#include "Misc.BloomBlur.slang.h"
#include "Misc.BloomComposite.slang.h"
#include "Misc.BloomExtract.slang.h"

#include <string_view>

namespace ya
{

struct BloomPostprocessing
{
    static constexpr EFormat::T BLOOM_FORMAT = EFormat::R16G16B16A16_SFLOAT;
    static constexpr std::string_view kOutputExportName   = "Bloom.Output";
    static constexpr std::string_view kExtractExportName  = "Bloom.Extract";
    static constexpr std::string_view kBlurPingExportName = "Bloom.BlurPing";
    static constexpr std::string_view kBlurPongExportName = "Bloom.BlurPong";

    struct InitDesc
    {
        IRender*              render                = nullptr;
        PipelineRenderingInfo pipelineRenderingInfo = {};
    };

    struct RenderDesc
    {
        ICommandBuffer*            cmdBuf            = nullptr;
        Texture*                   sceneTexture      = nullptr;
        RenderTexture*             sceneImage        = nullptr;
        RGTextureHandle            sceneHandle{};
        Extent2D                   renderExtent      = {};
        const PostProcessingState* state             = nullptr;
        uint64_t                   viewId            = 0;
        BloomPassBindings          bloom{};
    };

    IRender* _render = nullptr;
    InitDesc _initDesc{};

    stdptr<IDescriptorSetLayout> _extractDSL;
    stdptr<IPipelineLayout>      _extractPPL;
    stdptr<IGraphicsPipeline>    _extractPipeline;

    stdptr<IDescriptorSetLayout> _blurDSL;
    stdptr<IPipelineLayout>      _blurPPL;
    stdptr<IGraphicsPipeline>    _blurPipeline;

    stdptr<IDescriptorSetLayout> _compositeDSL;
    stdptr<IPipelineLayout>      _compositePPL;
    stdptr<IGraphicsPipeline>    _compositePipeline;

    uint32_t _lastBlurPassCount = 0;
    stdptr<RenderTexture> _extractImage;
    stdptr<RenderTexture> _blurPingImage;
    stdptr<RenderTexture> _blurPongImage;
    stdptr<RenderTexture> _compositeImage;
    std::unique_ptr<RenderGraphExecutor> _graphExecutor;

    void init(const InitDesc& initDesc);
    void shutdown();
    void beginFrame();
    RGTextureHandle appendGraphPasses(RenderGraph& graph, const RenderDesc& desc);
    void capturePreparedResources(const RenderGraphExecutionResult& result);
    void clearPreparedResources();
    void render(const RenderDesc& desc);
    [[nodiscard]] stdptr<RenderTexture> getExtractImageShared() const { return _extractImage; }
    [[nodiscard]] stdptr<RenderTexture> getBlurImageShared() const { return _blurPongImage ? _blurPongImage : _blurPingImage; }
    [[nodiscard]] stdptr<RenderTexture> getCompositeImageShared() const { return _compositeImage; }
    [[nodiscard]] stdptr<IDescriptorSetLayout> getExtractDSL() const { return _extractDSL; }
    [[nodiscard]] stdptr<IDescriptorSetLayout> getBlurDSL() const { return _blurDSL; }
    [[nodiscard]] stdptr<IDescriptorSetLayout> getCompositeDSL() const { return _compositeDSL; }

  private:
    void initExtractPipeline();
    void initBlurPipeline();
    void initCompositePipeline();
    void writeSampler(DescriptorSetHandle set, IImageView* inputImageView);
    void writeComposite(DescriptorSetHandle set, IImageView* sceneImageView, IImageView* bloomImageView);
};

} // namespace ya
