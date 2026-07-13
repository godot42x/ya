#pragma once

#include "Core/Base.h"
#include "Render/Core/RenderGraphExecutor.h"
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

    void     init(const InitDesc& desc);
    void     shutdown();
    void     beginFrame();
    void     renderGUI();
    void     renderSettingsGUI();
    void     renderTechnicalGUI();
    RGTextureHandle appendGraphPasses(RenderGraph& graph,
                                      Texture*      inputTexture,
                                      glm::vec2     viewportExtent,
                                      FrameContext* ctx);
    void     resolvePreparedResources(const RenderGraphResourceRegistry& registry);
    void     clearPreparedResources();
    RenderImage* execute(ICommandBuffer* cmdBuf,
                         Texture*        inputTexture,
                         glm::vec2       viewportExtent,
                         FrameContext*   ctx);

    [[nodiscard]] bool                       isEnabled() const { return bEnabled; }
    [[nodiscard]] RenderImage*               getBloomExtractImage() const { return _bloomProcessor ? _bloomProcessor->getExtractImage() : nullptr; }
    [[nodiscard]] RenderImage*               getBloomBlurImage() const { return _bloomProcessor ? _bloomProcessor->getBlurImage() : nullptr; }
    [[nodiscard]] RenderImage*               getBloomCompositeImage() const { return _bloomProcessor ? _bloomProcessor->getCompositeImage() : nullptr; }
    [[nodiscard]] PostProcessingState&       getState() { return _state; }
    [[nodiscard]] const PostProcessingState& getState() const { return _state; }
};

} // namespace ya
