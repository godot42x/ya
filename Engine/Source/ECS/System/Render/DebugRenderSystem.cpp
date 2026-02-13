#include "DebugRenderSystem.h"

#include "Core/App/App.h"
#include "ECS/Component/MeshComponent.h"
#include "ECS/Component/TransformComponent.h"
#include "Render/Core/Swapchain.h"
#include "Render/Render.h"
#include "Scene/Scene.h"

namespace ya
{

void DebugRenderSystem::onInit(IRenderPass* renderPass, const PipelineRenderingInfo& pipelineRenderingInfo)
{


    auto* render = App::get()->getRender();
    YA_CORE_ASSERT(render != nullptr, "DebugRenderSystem::onInit render is null");

    auto dslVec = IDescriptorSetLayout::create(render, _pipelineLayoutDesc.descriptorSetLayouts);
    _dsl        = dslVec[0];

    _pipelineLayout = IPipelineLayout::create(
        render,
        _pipelineLayoutDesc.label,
        _pipelineLayoutDesc.pushConstants,
        dslVec);

    _pipelineDesc = GraphicsPipelineCreateInfo{
        .renderPass            = renderPass,
        .pipelineRenderingInfo = pipelineRenderingInfo,
        .pipelineLayout        = _pipelineLayout.get(),

        .shaderDesc = ShaderDesc{
            .shaderName        = "Test/DebugRender.glsl",
            .bDeriveFromShader = false,
            .vertexBufferDescs = {
                VertexBufferDescription{
                    .slot  = 0,
                    .pitch = sizeof(ya::Vertex),
                },
            },
            .vertexAttributes = {
                VertexAttribute{
                    .bufferSlot = 0,
                    .location   = 0,
                    .format     = EVertexAttributeFormat::Float3,
                    .offset     = offsetof(ya::Vertex, position),
                },
                VertexAttribute{
                    .bufferSlot = 0,
                    .location   = 1,
                    .format     = EVertexAttributeFormat::Float2,
                    .offset     = offsetof(ya::Vertex, texCoord0),
                },
                VertexAttribute{
                    .bufferSlot = 0,
                    .location   = 2,
                    .format     = EVertexAttributeFormat::Float3,
                    .offset     = offsetof(ya::Vertex, normal),
                },
            },
        },
        .dynamicFeatures = {
            EPipelineDynamicFeature::Scissor,
            EPipelineDynamicFeature::Viewport,
#if !NOT_DYN_CULL
            EPipelineDynamicFeature::CullMode,
#endif
        },
        .primitiveType      = EPrimitiveType::TriangleList,
        .rasterizationState = RasterizationState{
            .polygonMode = EPolygonMode::Fill,
            .cullMode    = _cullMode,
            .frontFace   = EFrontFaceType::CounterClockWise,
        },
        .depthStencilState = DepthStencilState{
            .bDepthTestEnable       = true,
            .bDepthWriteEnable      = false,
            .depthCompareOp         = ECompareOp::LessOrEqual,
            .bDepthBoundsTestEnable = false,
            .bStencilTestEnable     = false,
            .minDepthBounds         = 0.0f,
            .maxDepthBounds         = 1.0f,
        },
        .colorBlendState = ColorBlendState{
            .attachments = {
                ColorBlendAttachmentState{
                    .index               = 0,
                    .bBlendEnable        = false,
                    .srcColorBlendFactor = EBlendFactor::One,
                    .dstColorBlendFactor = EBlendFactor::Zero,
                    .colorBlendOp        = EBlendOp::Add,
                    .srcAlphaBlendFactor = EBlendFactor::One,
                    .dstAlphaBlendFactor = EBlendFactor::Zero,
                    .alphaBlendOp        = EBlendOp::Add,
                    .colorWriteMask      = EColorComponent::R | EColorComponent::G | EColorComponent::B | EColorComponent::A,
                },
            },
        },
        .viewportState = ViewportState{
            .viewports = {
                {
                    .x        = 0,
                    .y        = 0,
                    .width    = static_cast<float>(render->getSwapchain()->getExtent().width),
                    .height   = static_cast<float>(render->getSwapchain()->getExtent().height),
                    .minDepth = 0.0f,
                    .maxDepth = 1.0f,
                },
            },
            .scissors = {Scissor{
                .offsetX = 0,
                .offsetY = 0,
                .width   = render->getSwapchain()->getExtent().width,
                .height  = render->getSwapchain()->getExtent().height,
            }},
        },
    };

    _pipeline = IGraphicsPipeline::create(render);
    _pipeline->recreate(_pipelineDesc);

    _dsp = IDescriptorPool::create(
        render,
        DescriptorPoolCreateInfo{
            .maxSets   = 1,
            .poolSizes = {
                DescriptorPoolSize{
                    .type            = EPipelineDescriptorType::UniformBuffer,
                    .descriptorCount = 1,
                },
            },
        });

    _uboDS = _dsp->allocateDescriptorSets(_dsl);
    _ubo   = IBuffer::create(
        render,
        BufferCreateInfo{
              .label         = "DebugRender_UBO",
              .usage         = EBufferUsage::UniformBuffer,
              .size          = sizeof(DebugUBO),
              .memProperties = EMemoryProperty::HostVisible | EMemoryProperty::HostCoherent,
        });

    render->getDescriptorHelper()->updateDescriptorSets(
        {
            IDescriptorSetHelper::genSingleBufferWrite(_uboDS, 0, EPipelineDescriptorType::UniformBuffer, _ubo.get()),
        },
        {});
}

void DebugRenderSystem::onDestroy()
{
    _ubo.reset();
    _dsp.reset();
    _dsl.reset();
    _pipelineLayout.reset();
    _pipeline.reset();
}



void DebugRenderSystem::updateUBO(FrameContext* ctx)
{
    auto* app         = getApp();
    uDebug.projection = ctx->projection;
    uDebug.view       = ctx->view;
    uDebug.resolution = glm::ivec2(static_cast<int>(ctx->extent.width), static_cast<int>(ctx->extent.height));
    uDebug.time       = static_cast<float>(app->getElapsedTimeMS()) / 1000.0f;

    _ubo->writeData(&uDebug, sizeof(DebugUBO), 0);
}

void DebugRenderSystem::onRender(ICommandBuffer* cmdBuf, FrameContext* ctx)
{
    auto* scene = getActiveScene();
    if (!scene) {
        return;
    }
    auto view = scene->getRegistry().view<MeshComponent, TransformComponent>();
    if (view.begin() == view.end()) {
        return;
    }
    const uint32_t width  = ctx->extent.width;
    const uint32_t height = ctx->extent.height;
    if (width == 0 || height == 0) {
        return;
    }

    updateUBO(ctx);

    cmdBuf->bindPipeline(_pipeline.get());

    float viewportY      = 0.0f;
    float viewportHeight = static_cast<float>(height);
    if (bReverseViewportY) {
        viewportY      = static_cast<float>(height);
        viewportHeight = -static_cast<float>(height);
    }

    cmdBuf->setViewport(0.0f, viewportY, static_cast<float>(width), viewportHeight, 0.0f, 1.0f);
    cmdBuf->setScissor(0, 0, width, height);
#if !NOT_DYN_CULL
    cmdBuf->setCullMode(_cullMode);
#endif

    for (auto [entity, meshComp, tc] : view.each()) {
        auto* mesh = meshComp.getMesh();
        if (!mesh) {
            continue;
        }

        ModelPushConstant pushConst{
            .modelMat = tc.getTransform(),
        };

        cmdBuf->bindDescriptorSets(_pipelineLayout.get(), 0, {_uboDS});
        cmdBuf->pushConstants(_pipelineLayout.get(),
                              EShaderStage::Vertex,
                              0,
                              sizeof(ModelPushConstant),
                              &pushConst);

        mesh->draw(cmdBuf);
    }
}

void DebugRenderSystem::onRenderGUI()
{
    ImGui::InputInt("Debug Mode", &uDebug.mode);
    ImGui::DragFloat4("Float Param", glm::value_ptr(uDebug.floatParam), 0.1f);
}

} // namespace ya
