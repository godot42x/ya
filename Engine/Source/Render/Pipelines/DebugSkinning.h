#pragma once

#include "Core/Math/Geometry.h"
#include "Render/Core/CommandBuffer.h"
#include "Render/Core/IRenderTarget.h"
#include "Render/Core/Pipeline.h"
#include "Render/Mesh.h"

#include <glm/glm.hpp>

#include "Misc.DebugSkinning.slang.h"

namespace ya
{

struct DebugSkinning
{
    using PushConstant = slang_types::Misc::DebugSkinning::PushConstants;

    static constexpr EFormat::T LINEAR_FORMAT = EFormat::R16G16B16A16_SFLOAT;
    static constexpr EFormat::T DEPTH_FORMAT  = EFormat::D32_SFLOAT;
    static constexpr int32_t    BONE_COUNT    = 64;

    IRender*                  _render = nullptr;
    stdptr<IGraphicsPipeline> _pipeline;
    stdptr<IPipelineLayout>   _pipelineLayout;
    bool                      bEnabled          = false;
    bool                      bReverseViewportY = true;
    int32_t                   pickingBone       = 0;

    void init(IRender* render)
    {
        _render         = render;
        _pipelineLayout = IPipelineLayout::create(
            _render,
            "DebugSkinning_PPL",
            {PushConstantRange{.offset = 0, .size = sizeof(PushConstant), .stageFlags = EShaderStage::Vertex | EShaderStage::Fragment}},
            {});

        GraphicsPipelineCreateInfo ci{};
        ci.renderPass = nullptr;
        ci.shaderDesc = {
            .shaderName        = "Misc/DebugSkinning.slang",
            .vertexBufferDescs = {
                VertexBufferDescription{.slot = 0, .pitch = sizeof(ya::Vertex)},
                VertexBufferDescription{.slot = 1, .pitch = sizeof(ya::SkeletonMeshVertex)},
            },
            .vertexAttributes = {
                {
                    {.bufferSlot = 0, .location = 0, .format = EVertexAttributeFormat::Float3, .offset = offsetof(ya::Vertex, position)},
                    {.bufferSlot = 1, .location = 4, .format = EVertexAttributeFormat::Int32x4, .offset = offsetof(ya::SkeletonMeshVertex, boneIDs)},
                    {.bufferSlot = 1, .location = 5, .format = EVertexAttributeFormat::Float4, .offset = offsetof(ya::SkeletonMeshVertex, weights)},
                },
            },
        };
        ci.pipelineRenderingInfo = {
            .label                  = "DebugSkinningPipeline",
            .colorAttachmentFormats = {LINEAR_FORMAT},
            .depthAttachmentFormat  = DEPTH_FORMAT,
        };
        ci.pipelineLayout                      = _pipelineLayout.get();
        ci.dynamicFeatures                     = {EPipelineDynamicFeature::Viewport, EPipelineDynamicFeature::Scissor};
        ci.primitiveType                       = EPrimitiveType::TriangleList;
        ci.rasterizationState                  = RasterizationState::defaultCullBackWithFrontCCW();
        ci.depthStencilState                   = DepthStencilState::defaultDrawing();
        ci.depthStencilState.bDepthWriteEnable = false;
        ci.depthStencilState.depthCompareOp    = ECompareOp::LessOrEqual;
        ci.colorBlendState                     = {
            .attachments = {
                {
                    .index               = 0,
                    .bBlendEnable        = true,
                    .srcColorBlendFactor = EBlendFactor::SrcAlpha,
                    .dstColorBlendFactor = EBlendFactor::OneMinusSrcAlpha,
                    .colorBlendOp        = EBlendOp::Add,
                    .srcAlphaBlendFactor = EBlendFactor::SrcAlpha,
                    .dstAlphaBlendFactor = EBlendFactor::OneMinusSrcAlpha,
                    .alphaBlendOp        = EBlendOp::Add,
                    .colorWriteMask      = EColorComponent::R | EColorComponent::G | EColorComponent::B | EColorComponent::A,
                },
            },
        };
        ci.viewportState = {.viewports = {Viewport::defaults()}, .scissors = {Scissor::defaults()}};

        _pipeline = IGraphicsPipeline::create(render);
        _pipeline->recreate(ci);
    }

    void destroy()
    {
        _pipeline.reset();
        _pipelineLayout.reset();
        _render = nullptr;
    }

    void beginFrame()
    {
        if (_pipeline) {
            _pipeline->beginFrame();
        }
    }

    void refreshPipelineFormats(const IRenderTarget* viewportRT)
    {
        if (!_pipeline || !viewportRT) {
            return;
        }

        const auto& colorDescs = viewportRT->getColorAttachmentDescs();
        const auto& depthDesc  = viewportRT->getDepthAttachmentDesc();
        if (colorDescs.empty()) {
            return;
        }

        auto ci                                         = _pipeline->getDesc();
        ci.pipelineRenderingInfo.colorAttachmentFormats = {colorDescs.front().format};
        ci.pipelineRenderingInfo.depthAttachmentFormat  = depthDesc.has_value() ? depthDesc->format : EFormat::Undefined;
        _pipeline->updateDesc(std::move(ci));
    }

    void draw(ICommandBuffer*  cmdBuf,
              Mesh*            mesh,
              uint32_t         viewportWidth,
              uint32_t         viewportHeight,
              const glm::mat4& projection,
              const glm::mat4& view,
              const glm::mat4& model) const
    {
        if (!bEnabled || !_pipeline || !_pipelineLayout || !cmdBuf || !mesh || viewportWidth == 0 || viewportHeight == 0) {
            return;
        }

        cmdBuf->bindPipeline(_pipeline.get());

        float viewportY            = 0.0f;
        float viewportHeightSigned = static_cast<float>(viewportHeight);
        if (bReverseViewportY) {
            viewportY            = static_cast<float>(viewportHeight);
            viewportHeightSigned = -viewportHeightSigned;
        }
        cmdBuf->setViewport(0.0f, viewportY, static_cast<float>(viewportWidth), viewportHeightSigned, 0.0f, 1.0f);
        cmdBuf->setScissor(0, 0, viewportWidth, viewportHeight);

        PushConstant pc{
            .projection  = projection,
            .view        = view,
            .model       = model,
            .pickingBone = pickingBone,
        };
        cmdBuf->pushConstants(_pipelineLayout.get(), EShaderStage::Vertex | EShaderStage::Fragment, 0, sizeof(PushConstant), &pc);
        mesh->draw(cmdBuf);
    }

    void renderGUI();
};

} // namespace ya
