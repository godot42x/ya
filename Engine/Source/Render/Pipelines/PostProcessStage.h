#pragma once

#include "Core/Base.h"
#include "Render/Core/Swapchain.h"
#include "Render/Core/Texture.h"
#include "Render/Pipelines/BasicPostprocessing.h"
#include "Render/Render.h"
#include "Render/RenderDefines.h"

namespace ya
{

/// Reusable post-processing stage that can be composed into any render pipeline.
/// Owns the postprocess render texture, BasicPostprocessing system, and the
/// endViewportPass() logic that runs after the main viewport pass closes.
struct PostProcessStage
{
  private:
    IRender*              _render      = nullptr;
    EFormat::T            _colorFormat = EFormat::R8G8B8A8_UNORM;
    stdptr<IRenderSystem> _system;
    stdptr<Texture>       _postprocessTexture;

  public:
    struct InitDesc
    {
        IRender*   render = nullptr;
        EFormat::T colorFormat;
        uint32_t   width  = 0;
        uint32_t   height = 0;
    };

    void init(const InitDesc& desc)
    {
        _render      = desc.render;
        _colorFormat = desc.colorFormat;

        _postprocessTexture = Texture::createRenderTexture(RenderTextureCreateInfo{
            .label   = "PostprocessRT",
            .width   = desc.width,
            .height  = desc.height,
            .format  = desc.colorFormat,
            .usage   = EImageUsage::ColorAttachment | EImageUsage::Sampled,
            .samples = ESampleCount::Sample_1,
            .isDepth = false,
        });

        _system = ya::makeShared<BasicPostprocessing>();
        _system->init(IRenderSystem::InitParams{
            .renderPass            = nullptr,
            .pipelineRenderingInfo = PipelineRenderingInfo{
                .label                   = "BasicPostprocessing",
                .viewMask                = 0,
                .colorAttachmentFormats  = {desc.colorFormat},
                .depthAttachmentFormat   = EFormat::Undefined,
                .stencilAttachmentFormat = EFormat::Undefined,
            },
        });
    }

    void shutdown()
    {
        if (_system) {
            _system->onDestroy();
            _system.reset();
        }
        _postprocessTexture.reset();
    }

    void beginFrame()
    {
        if (_system) {
            _system->beginFrame();
        }
    }

    void renderGUI()
    {
        if (_system) {
            _system->renderGUI();
        }
    }

    /// Run postprocessing after the main viewport pass has been closed.
    /// @param cmdBuf        Active command buffer.
    /// @param inputTexture  The viewport color texture to read from.
    /// @param viewportRect  Viewport extent (used for rendering area).
    /// @param dt            Delta time.
    /// @param ctx           Frame context for the postprocessing system.
    /// @return The final output texture (postprocess result if enabled, or inputTexture if disabled).
    Texture* execute(ICommandBuffer* cmdBuf,
                     Texture*        inputTexture,
                     glm::vec2       viewportExtent,
                     float           dt,
                     FrameContext*   ctx)
    {
        if (!_system || !_system->bEnabled) {
            return inputTexture;
        }

        cmdBuf->debugBeginLabel("Postprocessing");
        cmdBuf->transitionImageLayoutAuto(_postprocessTexture->image.get(), EImageLayout::ColorAttachmentOptimal);

        RenderingInfo ri{
            .label      = "Postprocessing",
            .renderArea = Rect2D{
                .pos    = {0, 0},
                .extent = viewportExtent,
            },
            .layerCount       = 1,
            .colorClearValues = {ClearValue(0.0f, 0.0f, 0.0f, 1.0f)},
            .depthClearValue  = ClearValue(1.0f, 0),
            .colorAttachments = {
                RenderingInfo::ImageSpec{
                    .texture = _postprocessTexture.get(),
                    .loadOp  = EAttachmentLoadOp::Clear,
                    .storeOp = EAttachmentStoreOp::Store,
                },
            },
        };

        cmdBuf->beginRendering(ri);

        auto* postprocessSystem = _system->as<BasicPostprocessing>();
        auto  swapchainFormat   = _render->getSwapchain()->getFormat();
        bool  bOutputIsSRGB     =  EFormat::isSRGB(swapchainFormat);
        postprocessSystem->setOutputColorSpace(bOutputIsSRGB);
        postprocessSystem->setInputTexture(inputTexture->getImageView(), Extent2D::fromVec2(viewportExtent));
        postprocessSystem->tick(cmdBuf, dt, ctx);

        cmdBuf->endRendering(ri);

        cmdBuf->transitionImageLayoutAuto(_postprocessTexture->image.get(), EImageLayout::ShaderReadOnlyOptimal);
        cmdBuf->debugEndLabel();

        return _postprocessTexture.get();
    }

    void onViewportResized(Extent2D newExtent)
    {
        if (!_render || !_postprocessTexture || newExtent.width == 0 || newExtent.height == 0) {
            return;
        }
        _render->waitIdle();
        _postprocessTexture = Texture::createRenderTexture(RenderTextureCreateInfo{
            .label   = "PostprocessRT",
            .width   = newExtent.width,
            .height  = newExtent.height,
            .format  = _colorFormat,
            .usage   = EImageUsage::ColorAttachment | EImageUsage::Sampled,
            .samples = ESampleCount::Sample_1,
            .isDepth = false,
        });
        _render->waitIdle();
    }

    // --- Accessors ---
    bool     isEnabled() const { return _system && _system->bEnabled; }
    Texture* getOutputTexture() const { return _postprocessTexture.get(); }
};

} // namespace ya
