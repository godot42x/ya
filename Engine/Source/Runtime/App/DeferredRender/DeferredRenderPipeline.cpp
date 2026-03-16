#include "DeferredRenderPipeline.h"
#include "Runtime/App/App.h"

#include "ECS/Component/MeshComponent.h"
#include "ECS/Component/TransformComponent.h"

#include <imgui.h>
namespace ya

{

void DeferredRenderPipeline::tick(ICommandBuffer* cmdBuf)
{
    // Flush dirty pipeline at frame begin (auto-recreate if GUI changed params last frame)
    if (_bGBufferPipelineDirty) {
        _bGBufferPipelineDirty = false;
        _gBufferPassResult     = PipelineBuilder::create(_render, _gBufferCreateDesc);
    }

    auto app   = App::get();
    auto scene = app->getSceneManager()->getActiveScene();
    if (!scene) {
        return;
    }

    // 1. grab all mesh with * material, render to geometry
    cmdBuf->beginRendering(RenderingInfo{
        .label      = "GBuffer Pass",
        .renderArea = Rect2D{
            .pos    = {0, 0},
            .extent = _gBufferRT->getExtent().toVec2(),
        },
        .layerCount       = 1,
        .colorClearValues = {
            ClearValue(0.0f, 0.0f, 0.0f, 1.0f),
            ClearValue(0.0f, 0.0f, 0.0f, 1.0f),
            ClearValue(0.0f, 0.0f, 0.0f, 1.0f),
        },
        .depthClearValue = ClearValue(1.0f, 0),
        .renderTarget    = _gBufferRT.get(),
    });


    cmdBuf->bindPipeline(_gBufferPassResult.pipeline.get());
    // _gBufferPassResult.pipelineLayout
    // cmdBuf->bindDescriptorSets()
    using PC = slang_types::DeferredRender::GBufferPass::PushConstants;
    auto pl  = _gBufferPassResult.pipelineLayout;

    for (const auto& [entity, mc, tc] :
         scene->getRegistry().view<MeshComponent, TransformComponent>().each())
    {
        PC pc{
            .modelMat = tc.getTransform()};
        cmdBuf->pushConstants(pl.get(),
                              EShaderStage::Vertex,
                              0,
                              sizeof(PC),
                              &pc);
        mc.getMesh()->draw(cmdBuf);
    }


    cmdBuf->endRendering({.renderTarget = _gBufferRT.get()});
}

void DeferredRenderPipeline::createGBufferPipeline()
{
    // The entire pipeline is derived from the shader:
    //   - Push constants: [[vk::push_constant]] PushConstants in GBufferPass.slang
    //   - Descriptor sets: [[vk::binding(0, 1)]] Sampler2D etc.
    //   - Vertex inputs:   VertexInput struct
    _gBufferCreateDesc = PipelineBuilder::CreateDesc{
        .shaderName        = "DeferredRender/GBufferPass.slang",
        .renderTarget      = _gBufferRT.get(),
        .depthStencilState = DepthStencilState{
            .bDepthTestEnable  = true,
            .bDepthWriteEnable = true,
            .depthCompareOp    = ECompareOp::Less,
        },
    };
    _gBufferPassResult = PipelineBuilder::create(_render, _gBufferCreateDesc);
}

void DeferredRenderPipeline::renderGUI()
{
    if (!ImGui::CollapsingHeader("GBuffer Pipeline")) {
        return;
    }

    bool dirty = false;

    // -- Rasterization State --
    if (ImGui::TreeNode("Rasterization")) {
        int cullMode = static_cast<int>(_gBufferCreateDesc.rasterizationState.cullMode);
        if (ImGui::Combo("Cull Mode", &cullMode, "None\0Front\0Back\0")) {
            _gBufferCreateDesc.rasterizationState.cullMode = static_cast<ECullMode::T>(cullMode);
            dirty                                          = true;
        }

        int polyMode = static_cast<int>(_gBufferCreateDesc.rasterizationState.polygonMode);
        if (ImGui::Combo("Polygon Mode", &polyMode, "Fill\0Line\0Point\0")) {
            _gBufferCreateDesc.rasterizationState.polygonMode = static_cast<EPolygonMode::T>(polyMode);
            dirty                                             = true;
        }

        ImGui::TreePop();
    }

    // -- Depth Stencil State --
    if (ImGui::TreeNode("Depth Stencil")) {
        if (ImGui::Checkbox("Depth Test", &_gBufferCreateDesc.depthStencilState.bDepthTestEnable)) {
            dirty = true;
        }
        if (ImGui::Checkbox("Depth Write", &_gBufferCreateDesc.depthStencilState.bDepthWriteEnable)) {
            dirty = true;
        }

        int depthOp = static_cast<int>(_gBufferCreateDesc.depthStencilState.depthCompareOp);
        if (ImGui::Combo("Depth Compare Op", &depthOp, "Never\0Less\0Equal\0LessOrEqual\0Greater\0NotEqual\0GreaterOrEqual\0Always\0")) {
            _gBufferCreateDesc.depthStencilState.depthCompareOp = static_cast<ECompareOp::T>(depthOp);
            dirty                                               = true;
        }

        ImGui::TreePop();
    }

    if (dirty) {
        _bGBufferPipelineDirty = true;
    }
}

} // namespace ya
