#include "Render2D.h"

#include "RHI/Core/Buffer.h"
#include "RHI/Core/CommandBuffer.h"
#include "RHI/Core/DescriptorSet.h"
#include "RHI/Core/Pipeline.h"
#include "RHI/Core/RenderPass.h"
#include "RHI/Core/RenderResourceFactory.h"
#include "RHI/Render.h"
#include "RHI/RenderDefines.h"

#include "Core/Common/DeferredDeletionQueue.h"
#include "GUI/Runtime/Resource/FontManager.h"
#include "RHI/Backend/TextureLibrary.h"

#include "utility.cc/ranges.h"

#include <glm/gtc/constants.hpp>

#include <limits>

namespace ya
{

namespace
{

bool passUsesDepthlessScreenPipeline(ERender2DPassDomain domain)
{
    switch (domain) {
        case ERender2DPassDomain::GameUICompositor:
        case ERender2DPassDomain::EditorCanvas:
            return true;
        case ERender2DPassDomain::RuntimeOverlay:
        case ERender2DPassDomain::EditorViewport:
        case ERender2DPassDomain::Count:
            return false;
    }
    return false;
}

std::vector<VertexAttribute> buildQuadVertexAttributes()
{
    return std::vector<VertexAttribute>{
        VertexAttribute{
            .bufferSlot = 0,
            .location   = 0,
            .format     = EVertexAttributeFormat::Float3,
            .offset     = offsetof(FQuadRender::Vertex, pos),
        },
        VertexAttribute{
            .bufferSlot = 0,
            .location   = 1,
            .format     = EVertexAttributeFormat::Float4,
            .offset     = offsetof(FQuadRender::Vertex, color),
        },
        VertexAttribute{
            .bufferSlot = 0,
            .location   = 2,
            .format     = EVertexAttributeFormat::Float2,
            .offset     = offsetof(FQuadRender::Vertex, texCoord),
        },
        VertexAttribute{
            .bufferSlot = 0,
            .location   = 3,
            .format     = EVertexAttributeFormat::Uint,
            .offset     = offsetof(FQuadRender::Vertex, textureIdx),
        },
        VertexAttribute{
            .bufferSlot = 0,
            .location   = 4,
            .format     = EVertexAttributeFormat::Float3,
            .offset     = offsetof(FQuadRender::Vertex, worldCenter),
        },
        VertexAttribute{
            .bufferSlot = 0,
            .location   = 5,
            .format     = EVertexAttributeFormat::Float3,
            .offset     = offsetof(FQuadRender::Vertex, worldDirection),
        },
        VertexAttribute{
            .bufferSlot = 0,
            .location   = 6,
            .format     = EVertexAttributeFormat::Float2,
            .offset     = offsetof(FQuadRender::Vertex, worldSize),
        },
    };
}

ViewportState buildQuadViewportState()
{
    return ViewportState{
        .viewports = {Viewport{
            .x        = 0.0f,
            .y        = 0.0f,
            .width    = static_cast<float>(Render2D::session.windowWidth),
            .height   = static_cast<float>(Render2D::session.windowHeight),
            .minDepth = 0.0f,
            .maxDepth = 1.0f,
        }},
        .scissors = {Scissor{
            .offsetX = 0,
            .offsetY = 0,
            .width   = Render2D::session.windowWidth,
            .height  = Render2D::session.windowHeight,
        }},
    };
}

GraphicsPipelineCreateInfo buildQuadScreenPipelineCI(IPipelineLayout* pipelineLayout,
                                                     const std::string& label,
                                                     EFormat::T colorFormat,
                                                     EFormat::T depthFormat)
{
    return GraphicsPipelineCreateInfo{
        .subPassRef            = 0,
        .renderPass            = nullptr,
        .pipelineRenderingInfo = PipelineRenderingInfo{
            .label                  = label,
            .viewMask               = 0,
            .colorAttachmentFormats = {colorFormat},
            .depthAttachmentFormat  = depthFormat,
        },
        .pipelineLayout = pipelineLayout,
        .shaderDesc = ShaderDesc{
            .sourceMode        = ShaderDesc::ESourceMode::StageFiles,
            .stageFiles        = {
                ShaderDesc::StageFile{.stage = EShaderStage::Vertex, .file = "Sprite2D.slang", .entryName = "vertMain"},
                ShaderDesc::StageFile{.stage = EShaderStage::Fragment, .file = "Sprite2D.slang", .entryName = "fragMain"},
            },
            .vertexBufferDescs = {
                VertexBufferDescription{
                    .slot  = 0,
                    .pitch = sizeof(FQuadRender::Vertex),
                },
            },
            .vertexAttributes = buildQuadVertexAttributes(),
            .defines          = {
                std::format("TEXTURE_SET_SIZE {}", FQuadRender::TEXTURE_SET_SIZE),
            },
        },
        .dynamicFeatures = {
            EPipelineDynamicFeature::Viewport,
            EPipelineDynamicFeature::Scissor,
            EPipelineDynamicFeature::CullMode,
        },
        .primitiveType      = EPrimitiveType::TriangleList,
        .rasterizationState = RasterizationState{
            .polygonMode = EPolygonMode::Fill,
            .cullMode    = ECullMode::Back,
            .frontFace   = EFrontFaceType::CounterClockWise,
        },
        .multisampleState  = MultisampleState{},
        .depthStencilState = DepthStencilState{
            .bDepthTestEnable       = false,
            .bDepthWriteEnable      = false,
            .depthCompareOp         = ECompareOp::Always,
            .bDepthBoundsTestEnable = false,
            .bStencilTestEnable     = false,
            .minDepthBounds         = 0.0f,
            .maxDepthBounds         = 1.0f,
        },
        .colorBlendState = ColorBlendState{
            .bLogicOpEnable = false,
            .attachments    = {
                ColorBlendAttachmentState{
                    .index               = 0,
                    .bBlendEnable        = true,
                    .srcColorBlendFactor = EBlendFactor::SrcAlpha,
                    .dstColorBlendFactor = EBlendFactor::OneMinusSrcAlpha,
                    .colorBlendOp        = EBlendOp::Add,
                    .srcAlphaBlendFactor = EBlendFactor::One,
                    .dstAlphaBlendFactor = EBlendFactor::Zero,
                    .alphaBlendOp        = EBlendOp::Add,
                    .colorWriteMask      = static_cast<EColorComponent::T>(EColorComponent::R | EColorComponent::G | EColorComponent::B | EColorComponent::A),
                },
            },
        },
        .viewportState = buildQuadViewportState(),
    };
}

} // namespace

FRender2dDebugState Render2D::debug;
FRender2dSession    Render2D::session;
FQuadRender*        Render2D::quadData = nullptr;
FLineRender*        Render2D::lineData = nullptr;

void Render2D::init(IRender* render, EFormat::T colorFormat, EFormat::T depthFormat)
{
    quadData = new FQuadRender();
    quadData->init(render, colorFormat, depthFormat);

    lineData = new FLineRender();
    lineData->init(render, colorFormat, depthFormat);
}

void Render2D::destroy()
{
    lineData->destroy();
    delete lineData;
    lineData = nullptr;

    quadData->destroy();
    delete quadData;
    quadData = nullptr;
}

void Render2D::onUpdate(float dt)
{
    (void)dt;
}

void Render2D::onRender()
{
}

void Render2D::begin(const FRender2dContext& ctx)
{
    // A stale session means an earlier pass forgot to call end(); fail loudly
    // instead of leaking clip state and command buffer into the next pass.
    YA_CORE_ASSERT(session.curCmdBuf == nullptr,
                   "Render2D::begin called while a recording session is still active (missing end()?)");
    session.curCmdBuf    = ctx.cmdBuf;
    session.cam          = ctx.cam;
    session.windowHeight = ctx.windowHeight;
    session.windowWidth  = ctx.windowWidth;
    session.clipStack.clear();
    session.passDomain = ctx.passDomain;
    Extent2D extent{.width = session.windowWidth, .height = session.windowHeight};
    quadData->begin(extent);
    lineData->begin(ctx.passDomain);
}

void Render2D::end()
{
    quadData->end();
    lineData->flush(session.curCmdBuf, session.cam.viewProjection);

    session.curCmdBuf    = nullptr;
    session.windowWidth  = 0;
    session.windowHeight = 0;
}

void Render2D::preparePassPipeline(ERender2DPassDomain domain, EFormat::T colorFormat, EFormat::T depthFormat)
{
    if (quadData) {
        quadData->preparePassPipeline(domain, colorFormat, depthFormat);
    }
}

void Render2D::pushClipRect(const Rect2D& rect)
{
    // Intersect with the current clip so nested clips never exceed their parent.
    Rect2D clipped = rect;
    if (!session.clipStack.empty()) {
        const Rect2D& current = session.clipStack.back();
        const glm::vec2 curMax = current.pos + current.extent;
        const glm::vec2 rectMax = rect.pos + rect.extent;
        clipped.pos    = glm::max(rect.pos, current.pos);
        clipped.extent = glm::max(glm::vec2(0.0f), glm::min(rectMax, curMax) - clipped.pos);
    }

    const bool bClipChanged = session.clipStack.empty() ||
                              session.clipStack.back().pos != clipped.pos ||
                              session.clipStack.back().extent != clipped.extent;
    session.clipStack.push_back(clipped);
    if (bClipChanged && quadData && session.curCmdBuf) {
        // Flush so the pending vertices are drawn with the OLD scissor; the
        // next flush picks up the new clip.
        quadData->flush(session.curCmdBuf);
    }
}

void Render2D::popClipRect()
{
    if (session.clipStack.empty()) {
        return;
    }
    session.clipStack.pop_back();
    if (quadData && session.curCmdBuf) {
        quadData->flush(session.curCmdBuf);
    }
}

void FLineRender::init(IRender* render, EFormat::T colorFormat, EFormat::T depthFormat)
{
    _render = render;
    constexpr uint32_t resourceCount =
        static_cast<uint32_t>(ERender2DPassDomain::Count) * MAX_FLIGHTS_IN_FLIGHT;

    _descriptorPool = IDescriptorPool::create(
        render,
        DescriptorPoolCreateInfo{
            .maxSets   = resourceCount,
            .poolSizes = {
                DescriptorPoolSize{
                    .type            = EPipelineDescriptorType::UniformBuffer,
                    .descriptorCount = resourceCount,
                },
            },
        });

    _frameUboDSL = IDescriptorSetLayout::create(render, _pipelineDesc.descriptorSetLayouts[0]);
    std::vector<DescriptorSetHandle> descriptorSets;
    _descriptorPool->allocateDescriptorSets(_frameUboDSL, resourceCount, descriptorSets);
    for (size_t domain = 0; domain < static_cast<size_t>(ERender2DPassDomain::Count); ++domain) {
        for (uint32_t flight = 0; flight < MAX_FLIGHTS_IN_FLIGHT; ++flight) {
            auto& resources = _passResources[domain].flights[flight];
            resources.frameUboDS = descriptorSets[domain * MAX_FLIGHTS_IN_FLIGHT + flight];
            resources.frameUBOBuffer = render->getResourceFactory()->createBuffer(
                ya::BufferCreateInfo{
                    .label       = std::format("Sprite2D_Line_{}_{}_FrameUBO", domain, flight),
                    .usage       = EBufferUsage::UniformBuffer,
                    .size        = sizeof(FrameUBO),
                    .memoryUsage = EMemoryUsage::CpuToGpu,
                });
            resources.vertexBuffer = render->getResourceFactory()->createBuffer(
                ya::BufferCreateInfo{
                    .label       = std::format("Sprite2D_Line_{}_{}_VertexBuffer", domain, flight),
                    .usage       = EBufferUsage::VertexBuffer | EBufferUsage::TransferDst,
                    .size        = sizeof(FLineRender::Vertex) * MaxVertexCount,
                    .memoryUsage = EMemoryUsage::CpuToGpu,
                });
            resources.vertexPtrHead = resources.vertexBuffer->map<FLineRender::Vertex>();
        }
    }

    std::vector<std::shared_ptr<IDescriptorSetLayout>> dslVec = {_frameUboDSL};
    _pipelineLayout = IPipelineLayout::create(render, "Sprite2D_Line_PipelineLayout", _pipelineDesc.pushConstants, dslVec);

    const auto buildVertexAttributes = []()
    {
        return std::vector<VertexAttribute>{
            VertexAttribute{
                .bufferSlot = 0,
                .location   = 0,
                .format     = EVertexAttributeFormat::Float3,
                .offset     = offsetof(Vertex, pos),
            },
            VertexAttribute{
                .bufferSlot = 0,
                .location   = 1,
                .format     = EVertexAttributeFormat::Float4,
                .offset     = offsetof(Vertex, color),
            },
        };
    };

    const auto buildViewportState = []()
    {
        return ViewportState{
            .viewports = {Viewport{
                .x        = 0.0f,
                .y        = 0.0f,
                .width    = static_cast<float>(Render2D::session.windowWidth),
                .height   = static_cast<float>(Render2D::session.windowHeight),
                .minDepth = 0.0f,
                .maxDepth = 1.0f,
            }},
            .scissors  = {Scissor{
                .offsetX = 0,
                .offsetY = 0,
                .width   = Render2D::session.windowWidth,
                .height  = Render2D::session.windowHeight,
            }},
        };
    };

    _pipeline = IGraphicsPipeline::create(render);
    _pipeline->recreate(GraphicsPipelineCreateInfo{
        .subPassRef            = 0,
        .renderPass            = nullptr,
        .pipelineRenderingInfo = PipelineRenderingInfo{
            .label                  = "Sprite2D_Line_Pipeline",
            .viewMask               = 0,
            .colorAttachmentFormats = {colorFormat},
            .depthAttachmentFormat  = depthFormat,
        },
        .pipelineLayout = _pipelineLayout.get(),
        .shaderDesc = ShaderDesc{
            .sourceMode        = ShaderDesc::ESourceMode::StageFiles,
            .stageFiles        = {
                ShaderDesc::StageFile{.stage = EShaderStage::Vertex, .file = "Sprite2DLine.slang", .entryName = "vertLineMain"},
                ShaderDesc::StageFile{.stage = EShaderStage::Fragment, .file = "Sprite2DLine.slang", .entryName = "fragLineMain"},
            },
            .vertexBufferDescs = {
                VertexBufferDescription{
                    .slot  = 0,
                    .pitch = sizeof(FLineRender::Vertex),
                },
            },
            .vertexAttributes = buildVertexAttributes(),
        },
        .dynamicFeatures = {
            EPipelineDynamicFeature::Viewport,
            EPipelineDynamicFeature::Scissor,
        },
        .primitiveType      = EPrimitiveType::Line,
        .rasterizationState = RasterizationState{
            .polygonMode = EPolygonMode::Fill,
            .cullMode    = ECullMode::None,
            .frontFace   = EFrontFaceType::CounterClockWise,
        },
        .multisampleState  = MultisampleState{},
        .depthStencilState = DepthStencilState{
            // Depth-test debug lines against the scene depth (the composition
            // pass attaches the viewport depth buffer). Lines never write depth.
            .bDepthTestEnable       = true,
            .bDepthWriteEnable      = false,
            .depthCompareOp         = ECompareOp::LessOrEqual,
            .bDepthBoundsTestEnable = false,
            .bStencilTestEnable     = false,
            .minDepthBounds         = 0.0f,
            .maxDepthBounds         = 1.0f,
        },
        .colorBlendState = ColorBlendState{
            .bLogicOpEnable = false,
            .attachments    = {
                ColorBlendAttachmentState{
                    .index               = 0,
                    .bBlendEnable        = true,
                    .srcColorBlendFactor = EBlendFactor::SrcAlpha,
                    .dstColorBlendFactor = EBlendFactor::OneMinusSrcAlpha,
                    .colorBlendOp        = EBlendOp::Add,
                    .srcAlphaBlendFactor = EBlendFactor::One,
                    .dstAlphaBlendFactor = EBlendFactor::Zero,
                    .alphaBlendOp        = EBlendOp::Add,
                    .colorWriteMask      = static_cast<EColorComponent::T>(EColorComponent::R | EColorComponent::G | EColorComponent::B | EColorComponent::A),
                },
            },
        },
        .viewportState = buildQuadViewportState(),
    });

}

void FLineRender::destroy()
{
    for (auto& pass : _passResources) {
        for (auto& resources : pass.flights) {
            resources.vertexBuffer.reset();
            resources.vertexPtrHead = nullptr;
            resources.frameUBOBuffer.reset();
            resources.frameUboDS = {};
        }
    }
    vertexPtr     = nullptr;
    vertexPtrHead = nullptr;
    _frameUboDSL.reset();
    _descriptorPool.reset();
    _pipeline.reset();
    _pipelineLayout.reset();
}

void FLineRender::begin(ERender2DPassDomain domain)
{
    _activePassDomain  = domain;
    _activeFlightIndex = _render ? _render->getCurrentFrameIndex() % MAX_FLIGHTS_IN_FLIGHT : 0;
    auto& resources    = _passResources[static_cast<size_t>(_activePassDomain)].flights[_activeFlightIndex];
    vertexPtrHead      = resources.vertexPtrHead;
    vertexPtr   = vertexPtrHead;
    vertexCount = 0;
}

void FLineRender::flush(ICommandBuffer* cmdBuf, const glm::mat4& viewProj)
{
    if (!cmdBuf || vertexCount == 0) {
        return;
    }

    FrameUBO ubo{
        .viewProj = viewProj,
        .view     = Render2D::session.cam.view,
    };
    auto& resources = _passResources[static_cast<size_t>(_activePassDomain)].flights[_activeFlightIndex];
    resources.frameUBOBuffer->writeData(&ubo, sizeof(ubo), 0);

    DescriptorBufferInfo bufferInfo(
        BufferHandle(resources.frameUBOBuffer->getHandle()),
        0,
        static_cast<uint64_t>(sizeof(FrameUBO)));
    _render->getDescriptorHelper()->updateDescriptorSets(
        {
            IDescriptorSetHelper::genBufferWrite(resources.frameUboDS,
                                                 0,
                                                 0,
                                                 EPipelineDescriptorType::UniformBuffer,
                                                 {bufferInfo}),
        },
        {});
    resources.vertexBuffer->flush();

    cmdBuf->bindPipeline(_pipeline.get());
    cmdBuf->setViewport(0.0f,
                        static_cast<float>(Render2D::session.windowHeight),
                        static_cast<float>(Render2D::session.windowWidth),
                        -static_cast<float>(Render2D::session.windowHeight),
                        0.0f,
                        1.0f);
    cmdBuf->setScissor(0, 0, Render2D::session.windowWidth, Render2D::session.windowHeight);

    cmdBuf->bindDescriptorSets(_pipelineLayout.get(), 0, {resources.frameUboDS});
    cmdBuf->bindVertexBuffer(0, resources.vertexBuffer.get(), 0);
    cmdBuf->draw(vertexCount, 1, 0, 0);

    vertexPtr   = vertexPtrHead;
    vertexCount = 0;
}

void FLineRender::addLine(const glm::vec3& from, const glm::vec3& to, const glm::vec4& color)
{
    if (vertexCount + 2 > MaxVertexCount) {
        flush(Render2D::session.curCmdBuf, Render2D::session.cam.viewProjection);
    }

    *vertexPtr++ = Vertex{.pos = from, .color = color};
    *vertexPtr++ = Vertex{.pos = to, .color = color};
    vertexCount += 2;
}

void FLineRender::addWireBox(const glm::mat4& model, const glm::vec3& halfExtent, const glm::vec4& color)
{
    static constexpr std::array<glm::vec3, 8> corners = {{
        {-1.0f, -1.0f, -1.0f}, {1.0f, -1.0f, -1.0f}, {1.0f, 1.0f, -1.0f}, {-1.0f, 1.0f, -1.0f},
        {-1.0f, -1.0f, 1.0f},  {1.0f, -1.0f, 1.0f},  {1.0f, 1.0f, 1.0f},  {-1.0f, 1.0f, 1.0f},
    }};
    static constexpr std::array<std::array<uint8_t, 2>, 12> edges = {{
        {{0, 1}}, {{1, 2}}, {{2, 3}}, {{3, 0}},
        {{4, 5}}, {{5, 6}}, {{6, 7}}, {{7, 4}},
        {{0, 4}}, {{1, 5}}, {{2, 6}}, {{3, 7}},
    }};

    for (const auto& edge : edges) {
        const glm::vec3 from = glm::vec3(model * glm::vec4(corners[edge[0]] * halfExtent, 1.0f));
        const glm::vec3 to   = glm::vec3(model * glm::vec4(corners[edge[1]] * halfExtent, 1.0f));
        addLine(from, to, color);
    }
}

void FLineRender::addWireSphere(const glm::vec3& center, float radius, const glm::vec4& color)
{
    static constexpr int   kSegmentCount = 24;
    static constexpr float kStep         = glm::two_pi<float>() / static_cast<float>(kSegmentCount);

    const auto addRing = [&](const glm::vec3& axisA, const glm::vec3& axisB)
    {
        for (int i = 0; i < kSegmentCount; ++i) {
            const float a0 = static_cast<float>(i) * kStep;
            const float a1 = static_cast<float>(i + 1) * kStep;
            const glm::vec3 from = center + radius * (axisA * glm::cos(a0) + axisB * glm::sin(a0));
            const glm::vec3 to   = center + radius * (axisA * glm::cos(a1) + axisB * glm::sin(a1));
            addLine(from, to, color);
        }
    };

    addRing({1.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f}); // XY
    addRing({1.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 1.0f}); // XZ
    addRing({0.0f, 1.0f, 0.0f}, {0.0f, 0.0f, 1.0f}); // YZ
}

void FQuadRender::init(IRender* render, EFormat::T colorFormat, EFormat::T depthFormat)
{
    _render = render;
    constexpr uint32_t domainCount = static_cast<uint32_t>(ERender2DPassDomain::Count);
    constexpr uint32_t resourceCount = domainCount * MAX_FLIGHTS_IN_FLIGHT;

    _descriptorPool = IDescriptorPool::create(
        render,
        DescriptorPoolCreateInfo{
            .maxSets   = resourceCount * 4,
            .poolSizes = {
                DescriptorPoolSize{
                    .type            = EPipelineDescriptorType::UniformBuffer,
                    .descriptorCount = resourceCount * 2,
                },
                DescriptorPoolSize{
                    .type            = EPipelineDescriptorType::CombinedImageSampler,
                    .descriptorCount = resourceCount * TEXTURE_SET_SIZE * 2,
                },
            },
        });

    _frameUboDSL = IDescriptorSetLayout::create(render, _pipelineDesc.descriptorSetLayouts[0]);
    std::vector<ya::DescriptorSetHandle> descriptorSets;
    _descriptorPool->allocateDescriptorSets(
        _frameUboDSL,
        resourceCount * 2,
        descriptorSets);
    for (size_t domain = 0; domain < static_cast<size_t>(ERender2DPassDomain::Count); ++domain) {
        for (uint32_t flight = 0; flight < MAX_FLIGHTS_IN_FLIGHT; ++flight) {
            const size_t resourceIndex = domain * MAX_FLIGHTS_IN_FLIGHT + flight;
            auto& resources = _passResources[domain].flights[flight];
            resources.frameUboDS      = descriptorSets[resourceIndex * 2];
            resources.worldFrameUboDS = descriptorSets[resourceIndex * 2 + 1];
            resources.frameUBOBuffer = render->getResourceFactory()->createBuffer(
                ya::BufferCreateInfo{
                    .label       = std::format("Sprite2D_{}_{}_FrameUBO", domain, flight),
                    .usage       = EBufferUsage::UniformBuffer,
                    .size        = sizeof(FrameUBO),
                    .memoryUsage = EMemoryUsage::CpuToGpu,
                });
            resources.worldFrameUBOBuffer = render->getResourceFactory()->createBuffer(
                ya::BufferCreateInfo{
                    .label       = std::format("Sprite2D_{}_{}_WorldFrameUBO", domain, flight),
                    .usage       = EBufferUsage::UniformBuffer,
                    .size        = sizeof(FrameUBO),
                    .memoryUsage = EMemoryUsage::CpuToGpu,
                });
        }
    }

    _resourceDSL = IDescriptorSetLayout::create(render, _pipelineDesc.descriptorSetLayouts[1]);
    descriptorSets.clear();
    _descriptorPool->allocateDescriptorSets(
        _resourceDSL,
        resourceCount * 2,
        descriptorSets);
    for (size_t domain = 0; domain < static_cast<size_t>(ERender2DPassDomain::Count); ++domain) {
        for (uint32_t flight = 0; flight < MAX_FLIGHTS_IN_FLIGHT; ++flight) {
            const size_t resourceIndex = domain * MAX_FLIGHTS_IN_FLIGHT + flight;
            auto& resources = _passResources[domain].flights[flight];
            resources.resourceDS      = descriptorSets[resourceIndex * 2];
            resources.worldResourceDS = descriptorSets[resourceIndex * 2 + 1];
        }
    }

    std::vector<std::shared_ptr<IDescriptorSetLayout>> dslVec = {_frameUboDSL, _resourceDSL};
    _pipelineLayout = IPipelineLayout::create(render, "Sprite2D_PipelineLayout", _pipelineDesc.pushConstants, dslVec);

    preparePassPipeline(ERender2DPassDomain::RuntimeOverlay, colorFormat, depthFormat);
    preparePassPipeline(ERender2DPassDomain::GameUICompositor, colorFormat, depthFormat);

    _worldPipeline = IGraphicsPipeline::create(render);
    _worldPipeline->recreate(GraphicsPipelineCreateInfo{
        .subPassRef            = 0,
        .renderPass            = nullptr,
        .pipelineRenderingInfo = PipelineRenderingInfo{
            .label                  = "Sprite2D_World_Pipeline",
            .viewMask               = 0,
            .colorAttachmentFormats = {colorFormat},
            .depthAttachmentFormat  = depthFormat,
        },
        .pipelineLayout = _pipelineLayout.get(),
        .shaderDesc = ShaderDesc{
            .sourceMode        = ShaderDesc::ESourceMode::StageFiles,
            .stageFiles        = {
                ShaderDesc::StageFile{.stage = EShaderStage::Vertex, .file = "Sprite2D.slang", .entryName = "vertWorldMain"},
                ShaderDesc::StageFile{.stage = EShaderStage::Fragment, .file = "Sprite2D.slang", .entryName = "fragMain"},
            },
            .vertexBufferDescs = {
                VertexBufferDescription{
                    .slot  = 0,
                    .pitch = sizeof(FQuadRender::Vertex),
                },
            },
            .vertexAttributes = buildQuadVertexAttributes(),
            .defines          = {
                std::format("TEXTURE_SET_SIZE {}", TEXTURE_SET_SIZE),
            },
        },
        .dynamicFeatures = {
            EPipelineDynamicFeature::Viewport,
            EPipelineDynamicFeature::Scissor,
            EPipelineDynamicFeature::CullMode,
        },
        .primitiveType      = EPrimitiveType::TriangleList,
        .rasterizationState = RasterizationState{
            .polygonMode = EPolygonMode::Fill,
            .cullMode    = ECullMode::Back,
            .frontFace   = EFrontFaceType::CounterClockWise,
        },
        .multisampleState  = MultisampleState{},
        .depthStencilState = DepthStencilState{
            .bDepthTestEnable       = false,
            .bDepthWriteEnable      = false,
            .depthCompareOp         = ECompareOp::Always,
            .bDepthBoundsTestEnable = false,
            .bStencilTestEnable     = false,
            .minDepthBounds         = 0.0f,
            .maxDepthBounds         = 1.0f,
        },
        .colorBlendState = ColorBlendState{
            .bLogicOpEnable = false,
            .attachments    = {
                ColorBlendAttachmentState{
                    .index               = 0,
                    .bBlendEnable        = true,
                    .srcColorBlendFactor = EBlendFactor::SrcAlpha,
                    .dstColorBlendFactor = EBlendFactor::OneMinusSrcAlpha,
                    .colorBlendOp        = EBlendOp::Add,
                    .srcAlphaBlendFactor = EBlendFactor::One,
                    .dstAlphaBlendFactor = EBlendFactor::Zero,
                    .alphaBlendOp        = EBlendOp::Add,
                    .colorWriteMask      = static_cast<EColorComponent::T>(EColorComponent::R | EColorComponent::G | EColorComponent::B | EColorComponent::A),
                },
            },
        },
        .viewportState = buildQuadViewportState(),
    });

    for (size_t domain = 0; domain < static_cast<size_t>(ERender2DPassDomain::Count); ++domain) {
        for (uint32_t flight = 0; flight < MAX_FLIGHTS_IN_FLIGHT; ++flight) {
            auto& resources = _passResources[domain].flights[flight];
            resources.vertexBuffer = render->getResourceFactory()->createBuffer(
                ya::BufferCreateInfo{
                    .label       = std::format("Sprite2D_{}_{}_Screen_VertexBuffer", domain, flight),
                    .usage       = EBufferUsage::VertexBuffer | EBufferUsage::TransferDst,
                    .size        = sizeof(FQuadRender::Vertex) * MaxVertexCount,
                    .memoryUsage = EMemoryUsage::CpuToGpu,
                });
            resources.vertexPtrHead = resources.vertexBuffer->map<FQuadRender::Vertex>();

            resources.worldVertexBuffer = render->getResourceFactory()->createBuffer(
                ya::BufferCreateInfo{
                    .label       = std::format("Sprite2D_{}_{}_World_VertexBuffer", domain, flight),
                    .usage       = EBufferUsage::VertexBuffer | EBufferUsage::TransferDst,
                    .size        = sizeof(FQuadRender::Vertex) * MaxVertexCount,
                    .memoryUsage = EMemoryUsage::CpuToGpu,
                });
            resources.worldVertexPtrHead = resources.worldVertexBuffer->map<FQuadRender::Vertex>();
        }
    }

    std::vector<uint32_t> indices(MaxIndexCount);
    for (uint32_t i = 0; i < MaxIndexCount; i += 6) {
        const uint32_t vertexIndex = (i / 6) * 4;
        indices[i + 0] = vertexIndex + 0;
        indices[i + 1] = vertexIndex + 1;
        indices[i + 2] = vertexIndex + 3;
        indices[i + 3] = vertexIndex + 0;
        indices[i + 4] = vertexIndex + 3;
        indices[i + 5] = vertexIndex + 2;
    }

    _indexBuffer = render->getResourceFactory()->createBuffer(
        ya::BufferCreateInfo{
            .label       = "Sprite2D_IndexBuffer",
            .usage       = EBufferUsage::IndexBuffer | EBufferUsage::TransferDst,
            .data        = indices.data(),
            .size        = sizeof(uint32_t) * MaxIndexCount,
            .memoryUsage = EMemoryUsage::GpuOnly,
        });
}

void FQuadRender::destroy()
{
    _indexBuffer.reset();

    for (auto& pass : _passResources) {
        for (auto& resources : pass.flights) {
            resources.vertexBuffer.reset();
            resources.vertexPtrHead = nullptr;
            resources.worldVertexBuffer.reset();
            resources.worldVertexPtrHead = nullptr;
            resources.frameUBOBuffer.reset();
            resources.worldFrameUBOBuffer.reset();
            resources.frameUboDS      = {};
            resources.worldFrameUboDS = {};
            resources.resourceDS      = {};
            resources.worldResourceDS = {};
        }
    }
    vertexPtr         = nullptr;
    vertexPtrHead     = nullptr;
    worldVertexPtr    = nullptr;
    worldVertexPtrHead = nullptr;
    _frameUboDSL.reset();

    _descriptorPool.reset();
    for (auto& pipelines : _passPipelines) {
        pipelines.screenPipeline.reset();
        pipelines.screenColorFormat = EFormat::Undefined;
        pipelines.screenDepthFormat = EFormat::Undefined;
        pipelines.uiPipeline.reset();
        pipelines.uiColorFormat = EFormat::Undefined;
    }
    _worldPipeline.reset();
    _pipelineLayout.reset();
}

void FQuadRender::preparePassPipeline(ERender2DPassDomain domain, EFormat::T colorFormat, EFormat::T depthFormat)
{
    if (!_render || colorFormat == EFormat::Undefined) {
        return;
    }

    auto& pipelines = _passPipelines[static_cast<size_t>(domain)];
    if (passUsesDepthlessScreenPipeline(domain)) {
        if (pipelines.uiPipeline && pipelines.uiColorFormat == colorFormat) {
            return;
        }

        auto pipeline = IGraphicsPipeline::create(_render);
        pipeline->recreate(buildQuadScreenPipelineCI(_pipelineLayout.get(),
                                                     std::format("Sprite2D_{}_UI_Pipeline", static_cast<size_t>(domain)),
                                                     colorFormat,
                                                     EFormat::Undefined));
        auto retired = std::move(pipelines.uiPipeline);
        pipelines.uiPipeline = std::move(pipeline);
        pipelines.uiColorFormat = colorFormat;
        if (DeferredDeletionQueue::get().isInitialized()) {
            DeferredDeletionQueue::get().retireResource(std::move(retired));
        }
        return;
    }

    if (pipelines.screenPipeline &&
        pipelines.screenColorFormat == colorFormat &&
        pipelines.screenDepthFormat == depthFormat) {
        return;
    }

    auto pipeline = IGraphicsPipeline::create(_render);
    pipeline->recreate(buildQuadScreenPipelineCI(_pipelineLayout.get(),
                                                 std::format("Sprite2D_{}_Screen_Pipeline", static_cast<size_t>(domain)),
                                                 colorFormat,
                                                 depthFormat));
    auto retired = std::move(pipelines.screenPipeline);
    pipelines.screenPipeline = std::move(pipeline);
    pipelines.screenColorFormat = colorFormat;
    pipelines.screenDepthFormat = depthFormat;
    if (DeferredDeletionQueue::get().isInitialized()) {
        DeferredDeletionQueue::get().retireResource(std::move(retired));
    }
}

void FQuadRender::begin(const Extent2D& extent)
{
    _activePassDomain = Render2D::session.passDomain;
    _activeFlightIndex = _render ? _render->getCurrentFrameIndex() % MAX_FLIGHTS_IN_FLIGHT : 0;
    auto& resources = activeFlightResources();
    vertexPtrHead      = resources.vertexPtrHead;
    vertexPtr          = vertexPtrHead;
    worldVertexPtrHead = resources.worldVertexPtrHead;
    worldVertexPtr     = worldVertexPtrHead;
    vertexCount        = 0;
    indexCount         = 0;
    worldVertexCount   = 0;
    worldIndexCount    = 0;
    _resourceVersion                    = 1;
    _uploadedScreenResourceVersion      = 0;
    _uploadedWorldResourceVersion       = 0;
    _frameUboUploaded                   = false;
    _worldFrameUboUploaded              = false;
    _textureBindings.clear();
    _textureLabel2Idx.clear();
    _textureBindings.push_back(TextureBinding{
        .texture = TextureLibrary::get().getWhiteTexture(),
        .sampler = TextureLibrary::get().getDefaultSampler(),
    });

    float w      = static_cast<float>(extent.width);
    float h      = static_cast<float>(extent.height);
    float aspect = w / h;
    if (w > h) {
        h = w / aspect;
    }
    else {
        w = h * aspect;
    }

    _screenOrthoProj = glm::orthoRH_ZO(0.0f, w, 0.0f, h, -1.0f, 1.0f);
}

void FQuadRender::end()
{
    flushWorld(Render2D::session.curCmdBuf);
    flush(Render2D::session.curCmdBuf);
}

void FQuadRender::flush(ICommandBuffer* cmdBuf)
{
    if (!cmdBuf || vertexCount == 0) {
        return;
    }

    // The UI compositor uses its OWN descriptor sets: it records in the same
    // command buffer as the world graph's overlay pass, and updating a
    // descriptor set that is already bound in an earlier region would
    // invalidate that region (no UPDATE_AFTER_BIND).
    auto& resources = activeFlightResources();
    resources.vertexBuffer->flush();

    auto& pipelines = activePassPipelines();
    IGraphicsPipeline* pipeline = passUsesDepthlessScreenPipeline(_activePassDomain)
                                      ? pipelines.uiPipeline.get()
                                      : pipelines.screenPipeline.get();
    YA_CORE_ASSERT(pipeline != nullptr,
                   "Render2D pipeline for pass domain {} was not prepared before command recording",
                   static_cast<size_t>(_activePassDomain));
    cmdBuf->bindPipeline(pipeline);
    cmdBuf->setViewport(0.0f,
                        0.0f,
                        static_cast<float>(Render2D::session.windowWidth),
                        static_cast<float>(Render2D::session.windowHeight),
                        0.0f,
                        1.0f);
    if (!Render2D::session.clipStack.empty()) {
        const Rect2D& clip = Render2D::session.clipStack.back();
        cmdBuf->setScissor(static_cast<int32_t>(clip.pos.x),
                           static_cast<int32_t>(clip.pos.y),
                           static_cast<int32_t>(clip.extent.x),
                           static_cast<int32_t>(clip.extent.y));
    }
    else {
        cmdBuf->setScissor(0, 0, Render2D::session.windowWidth, Render2D::session.windowHeight);
    }
    cmdBuf->setCullMode(Render2D::debug.screenCullMode);

    if (_uploadedScreenResourceVersion != _resourceVersion) {
        updateResources(resources.resourceDS);
        _uploadedScreenResourceVersion = _resourceVersion;
    }
    if (!_frameUboUploaded) {
        updateFrameUBO(resources.frameUBOBuffer, resources.frameUboDS, _screenOrthoProj, glm::mat4(1.0f));
        _frameUboUploaded = true;
    }

    const std::vector<DescriptorSetHandle> descriptorSets = {
        resources.frameUboDS,
        resources.resourceDS,
    };
    cmdBuf->bindDescriptorSets(_pipelineLayout.get(), 0, descriptorSets);
    cmdBuf->bindVertexBuffer(0, resources.vertexBuffer.get(), 0);
    cmdBuf->bindIndexBuffer(_indexBuffer.get(), 0, false);
    cmdBuf->drawIndexed(static_cast<uint32_t>(indexCount), 1, 0, 0, 0);

    vertexPtr   = vertexPtrHead;
    vertexCount = 0;
    indexCount  = 0;
}

void FQuadRender::flushWorld(ICommandBuffer* cmdBuf)
{
    if (!cmdBuf || worldVertexCount == 0) {
        return;
    }

    // YA_CORE_INFO("World overlay flush: {} vertices ({} quads), {} indices",
    //                worldVertexCount, worldVertexCount / 4, worldIndexCount);

    auto& resources = activeFlightResources();
    if (_uploadedWorldResourceVersion != _resourceVersion) {
        updateResources(resources.worldResourceDS);
        _uploadedWorldResourceVersion = _resourceVersion;
    }
    if (!_worldFrameUboUploaded) {
        updateFrameUBO(resources.worldFrameUBOBuffer,
                       resources.worldFrameUboDS,
                       Render2D::session.cam.viewProjection,
                       Render2D::session.cam.view);
        _worldFrameUboUploaded = true;
    }
    resources.worldVertexBuffer->flush();

    cmdBuf->bindPipeline(_worldPipeline.get());
    cmdBuf->setViewport(0.0f,
                        static_cast<float>(Render2D::session.windowHeight),
                        static_cast<float>(Render2D::session.windowWidth),
                        -static_cast<float>(Render2D::session.windowHeight),
                        0.0f,
                        1.0f);
    cmdBuf->setCullMode(Render2D::debug.worldCullMode);
    cmdBuf->setScissor(0, 0, Render2D::session.windowWidth, Render2D::session.windowHeight);

    std::vector<DescriptorSetHandle> descriptorSets = {
        resources.worldFrameUboDS,
        resources.worldResourceDS,
    };
    cmdBuf->bindDescriptorSets(_pipelineLayout.get(), 0, descriptorSets);
    cmdBuf->bindVertexBuffer(0, resources.worldVertexBuffer.get(), 0);
    cmdBuf->bindIndexBuffer(_indexBuffer.get(), 0, false);
    cmdBuf->drawIndexed(static_cast<uint32_t>(worldIndexCount), 1, 0, 0, 0);

    worldVertexPtr   = worldVertexPtrHead;
    worldVertexCount = 0;
    worldIndexCount  = 0;
}

void FQuadRender::updateFrameUBO(std::shared_ptr<IBuffer>& uboBuffer,
                                 DescriptorSetHandle       dsHandle,
                                 const glm::mat4&          viewProj,
                                 const glm::mat4&          view)
{
    FrameUBO ubo{
        .viewProj = viewProj,
        .view     = view,
    };
    uboBuffer->writeData(&ubo, sizeof(ubo), 0);

    DescriptorBufferInfo bufferInfo(BufferHandle(uboBuffer->getHandle()), 0, static_cast<uint64_t>(sizeof(FrameUBO)));

    _render->getDescriptorHelper()->updateDescriptorSets(
        {
            IDescriptorSetHelper::genBufferWrite(dsHandle,
                                                 0,
                                                 0,
                                                 EPipelineDescriptorType::UniformBuffer,
                                                 {bufferInfo}),
        },
        {});
}

void FQuadRender::updateResources(DescriptorSetHandle dsHandle)
{
    std::vector<DescriptorImageInfo> imageInfos;

    auto defaultSampler = TextureLibrary::get().getDefaultSampler();
    auto whiteTexture   = TextureLibrary::get().getWhiteTexture();

    for (uint32_t i = imageInfos.size(); i < TEXTURE_SET_SIZE; i++) {
        if (i < _textureBindings.size()) {
            auto& tb = _textureBindings[i];
            imageInfos.emplace_back(tb.getImageViewHandle(), tb.getSamplerHandle(), EImageLayout::ShaderReadOnlyOptimal);
        }
        else {
            imageInfos.emplace_back(whiteTexture->getImageView()->getHandle(),
                                    defaultSampler->getHandle(),
                                    EImageLayout::ShaderReadOnlyOptimal);
        }
    }

    _render->getDescriptorHelper()->updateDescriptorSets(
        {
            IDescriptorSetHelper::genImageWrite(dsHandle,
                                                0,
                                                0,
                                                EPipelineDescriptorType::CombinedImageSampler,
                                                imageInfos),
        },
        {});
}

uint32_t FQuadRender::findOrAddTexture(ya::Ptr<Texture> texture)
{
    uint32_t textureIdx = 0;
    if (texture) {
        auto it = _textureLabel2Idx.find(texture->getLabel());
        if (it != _textureLabel2Idx.end()) {
            textureIdx = it->second;
        }
        else {
            _textureBindings.push_back(TextureBinding{
                .texture = texture,
                .sampler = TextureLibrary::get().getDefaultSampler(),
            });
            auto idx                               = static_cast<uint32_t>(_textureBindings.size() - 1);
            _textureLabel2Idx[texture->getLabel()] = idx;
            textureIdx                             = idx;
            _lastPushTextureSlot                   = static_cast<int>(idx);
            ++_resourceVersion;
        }
    }
    return textureIdx;
}

void FQuadRender::drawTextureInternal(const glm::mat4& transform,
                                      uint32_t         textureIdx,
                                      const glm::vec3  tint,
                                      const glm::vec2& uvScale,
                                      const glm::vec2& uvTranslation)
{
    for (int i = 0; i < 4; i++) {
        *vertexPtr = FQuadRender::Vertex{
            .pos         = transform * FQuadRender::vertices[i],
            .color       = {tint, 1.0f},
            .texCoord    = FQuadRender::defaultTexcoord[i] * uvScale + uvTranslation,
            .textureIdx  = textureIdx,
            .worldCenter = glm::vec3(0.0f),
            .worldDirection = glm::vec3(0.0f, 0.0f, -1.0f),
            .worldSize   = glm::vec2(0.0f),
        };
        ++vertexPtr;
    }

    vertexCount += 4;
    indexCount += 6;
}

void FQuadRender::drawWorldTextureInternal(const glm::vec3&            center,
                                           const glm::vec3&            direction,
                                           const glm::vec2&            size,
                                           uint32_t                    textureIdx,
                                           const glm::vec3             tint,
                                           const glm::vec2&            uvScale)
{
    const glm::vec3 normalizedDirection = glm::length2(direction) > std::numeric_limits<float>::epsilon()
                                            ? glm::normalize(direction)
                                            : glm::vec3(0.0f, -1.0f, 0.0f);

    for (int i = 0; i < 4; i++) {
        *worldVertexPtr = FQuadRender::Vertex{
            .pos         = glm::vec3(FQuadRender::vertices[i]),
            .color       = {tint, 1.0f},
            .texCoord    = FQuadRender::defaultTexcoord[i] * uvScale,
            .textureIdx  = textureIdx,
            .worldCenter = center,
            .worldDirection = normalizedDirection,
            .worldSize   = size,
        };
        ++worldVertexPtr;
    }

    worldVertexCount += 4;
    worldIndexCount += 6;
}

void FQuadRender::drawTexture(const glm::vec3& position,
                              const glm::vec2& size,
                              ya::Ptr<Texture> texture,
                              const glm::vec4& tint,
                              const glm::vec2& uvScale)
{
    YA_CORE_ASSERT(Render2D::session.curCmdBuf != nullptr,
                   "Render2D draw called outside a begin()/end() recording session");
    if (shouldFlush()) {
        flush(Render2D::session.curCmdBuf);
    }

    glm::mat4 model = glm::translate(glm::mat4(1.f), {position.x, position.y, position.z}) *
                      glm::scale(glm::mat4(1.f), glm::vec3(size, 1.0f));

    uint32_t textureIdx = findOrAddTexture(texture);
    drawTextureInternal(model, textureIdx, tint, uvScale);
}

void FQuadRender::drawTexture(const glm::mat4& transform,
                              ya::Ptr<Texture> texture,
                              const glm::vec4& tint,
                              const glm::vec2& uvScale)
{
    YA_CORE_ASSERT(Render2D::session.curCmdBuf != nullptr,
                   "Render2D draw called outside a begin()/end() recording session");
    if (shouldFlush()) {
        flush(Render2D::session.curCmdBuf);
    }

    uint32_t textureIdx = findOrAddTexture(texture);
    drawTextureInternal(transform, textureIdx, tint, {uvScale.x, uvScale.y});
}

void FQuadRender::drawWorldTexture(const glm::vec3&            center,
                                   const glm::vec3&            direction,
                                   const glm::vec2&            size,
                                   ya::Ptr<Texture>            texture,
                                   const glm::vec4&            tint,
                                   const glm::vec2&            uvScale)
{
    YA_CORE_ASSERT(Render2D::session.curCmdBuf != nullptr,
                   "Render2D draw called outside a begin()/end() recording session");
    if (shouldFlushWorld()) {
        flushWorld(Render2D::session.curCmdBuf);
    }

    uint32_t textureIdx = findOrAddTexture(texture);
    drawWorldTextureInternal(center, direction, size, textureIdx, tint, {uvScale.x, uvScale.y});
}

void FQuadRender::drawSubTexture(const glm::vec3& position,
                                 const glm::vec2& size,
                                 ya::Ptr<Texture> texture,
                                 const glm::vec4& tint,
                                 const glm::vec4& uvRect)
{
    YA_CORE_ASSERT(Render2D::session.curCmdBuf != nullptr,
                   "Render2D draw called outside a begin()/end() recording session");
    if (shouldFlush()) {
        flush(Render2D::session.curCmdBuf);
    }

    uint32_t textureIdx = findOrAddTexture(texture);

    glm::mat4 model = glm::translate(glm::mat4(1.f), {position.x, position.y, position.z}) *
                      glm::scale(glm::mat4(1.f), glm::vec3(size, 1.0f));

    drawTextureInternal(model, textureIdx, tint, {uvRect.z, uvRect.w}, {uvRect.x, uvRect.y});
}

void FQuadRender::drawText(const std::string& text, const glm::vec3& position, const glm::vec4& color, Font* font)
{
    YA_CORE_ASSERT(Render2D::session.curCmdBuf != nullptr,
                   "Render2D draw called outside a begin()/end() recording session");
    float cursorX = position.x;
    float cursorY = position.y;

    YA_CORE_ASSERT(font != nullptr, "TODO: font is null in Render2D::drawText, should make a default font");
    YA_CORE_ASSERT(_render, "Render2D requires a render backend");
    FontManager::get()->ensureGlyphs(*_render, *font, text);

    const auto codePoints = utf8::decode(text);
    for (uint32_t codePoint : codePoints) {
        if (codePoint == '\r') {
            continue;
        }
        if (codePoint == '\n') {
            cursorX = position.x;
            cursorY += font->lineHeight;
            continue;
        }

        const Character& character = font->getCharacter(codePoint);
        if (codePoint == ' ') {
            cursorX += character.advance.x;
            continue;
        }
        if (codePoint == '\t') {
            cursorX += font->getCharacter(' ').advance.x * 4.0f;
            continue;
        }

        float xpos = cursorX + static_cast<float>(character.bearing.x);
        float ypos = cursorY + static_cast<float>(font->ascent - character.bearing.y);
        glm::vec3 pos  = glm::vec3(xpos, ypos, position.z);

        if (!character.bInAtlas) {
            if (character.standaloneTexture) {
                drawTexture(pos, glm::vec2(character.size), character.standaloneTexture, color);
            }
        }
        else {
            drawSubTexture(pos,
                           glm::vec2(character.size),
                           font->atlasTexture,
                           color,
                           character.uvRect);
        }

        cursorX += character.advance.x;
    }
}

} // namespace ya
