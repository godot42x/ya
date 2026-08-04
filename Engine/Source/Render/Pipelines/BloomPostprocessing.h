#pragma once

#include "Core/Base.h"
#include "Render/Core/DescriptorSet.h"
#include "Render/Core/Pipeline.h"
#include "Render/Core/RenderGraphExecutor.h"
#include "Render/Core/RenderImage.h"
#include "Runtime/Rendering/Common/PostProcessingState.h"

#include "Misc.BloomBlur.slang.h"
#include "Misc.BloomComposite.slang.h"
#include "Misc.BloomExtract.slang.h"

#include <string_view>

namespace ya
{

constexpr uint32_t MAX_BLOOM_BLUR_DESCRIPTOR_SETS = 64;

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
        RenderImage*               sceneImage        = nullptr;
        RGTextureHandle            sceneHandle{};
        Extent2D                   renderExtent      = {};
        const PostProcessingState* state            = nullptr;
    };

    IRender* _render = nullptr;
    InitDesc _initDesc{};

    stdptr<IDescriptorSetLayout> _extractDSL;
    stdptr<IDescriptorPool>      _extractDSP;
    stdptr<IPipelineLayout>      _extractPPL;
    stdptr<IGraphicsPipeline>    _extractPipeline;
    DescriptorSetHandle          _extractDS = nullptr;
    ImageViewHandle              _extractInputImageViewHandle = nullptr;

    stdptr<IDescriptorSetLayout> _blurDSL;
    stdptr<IDescriptorPool>      _blurDSP;
    stdptr<IPipelineLayout>      _blurPPL;
    stdptr<IGraphicsPipeline>    _blurPipeline;
    std::vector<DescriptorSetHandle> _blurDSs;
    std::vector<ImageViewHandle>     _blurInputImageViewHandles;

    stdptr<IDescriptorSetLayout> _compositeDSL;
    stdptr<IDescriptorPool>      _compositeDSP;
    stdptr<IPipelineLayout>      _compositePPL;
    stdptr<IGraphicsPipeline>    _compositePipeline;
    DescriptorSetHandle          _compositeDS = nullptr;
    ImageViewHandle              _compositeSceneImageViewHandle = nullptr;
    ImageViewHandle              _compositeBloomImageViewHandle = nullptr;

    uint32_t _lastBlurPassCount = 0;
    stdptr<RenderImage> _extractImage;
    stdptr<RenderImage> _blurPingImage;
    stdptr<RenderImage> _blurPongImage;
    stdptr<RenderImage> _compositeImage;
    std::unique_ptr<RenderGraphExecutor> _graphExecutor;

    void init(const InitDesc& initDesc);
    void shutdown();
    void beginFrame();
    RGTextureHandle appendGraphPasses(RenderGraph& graph, const RenderDesc& desc);
    void capturePreparedResources(const RenderGraphExecutionResult& result);
    void clearPreparedResources();
    void render(const RenderDesc& desc);
    [[nodiscard]] stdptr<RenderImage> getExtractImageShared() const { return _extractImage; }
    [[nodiscard]] stdptr<RenderImage> getBlurImageShared() const { return _blurPongImage ? _blurPongImage : _blurPingImage; }
    [[nodiscard]] stdptr<RenderImage> getCompositeImageShared() const { return _compositeImage; }

  private:
    void initExtractPipeline();
    void initBlurPipeline();
    void initCompositePipeline();
    void updateExtractDescriptor(IImageView* inputImageView);
    DescriptorSetHandle updateBlurDescriptor(uint32_t passIndex, IImageView* inputImageView);
    void updateCompositeDescriptor(IImageView* sceneImageView, IImageView* bloomImageView);
};

} // namespace ya
