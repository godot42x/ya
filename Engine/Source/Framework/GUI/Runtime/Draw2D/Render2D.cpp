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
#include "GUI/Resources/FontManager.h"
#include "RHI/Backend/TextureLibrary.h"

#include "utility.cc/ranges.h"

#include <glm/gtc/constants.hpp>

#include <limits>

namespace ya
{

namespace
{

ya::Ptr<Sampler> resolveSamplerForTexture(Texture* texture)
{
    if (!texture) {
        return TextureLibrary::get().getDefaultSampler();
    }

    const std::string& label = texture->getLabel();
    if (label.starts_with("FontAtlas_") || label.starts_with("FontGlyph_")) {
        return TextureLibrary::get().getClampLinearSampler();
    }

    return TextureLibrary::get().getDefaultSampler();
}

bool shouldReverseWorldViewport(IRender* render)
{
    if (!Render2D::debug.bReverseViewport) {
        return false;
    }
    return render && render->getAPI() == ERenderAPI::Vulkan;
}

void setScreenViewportAndScissor(ICommandBuffer& cmdBuf, IRender* render, uint32_t width, uint32_t height)
{
    // Screen-space UI still owns the app contract (top-left origin, Y-down),
    // but Vulkan's framebuffer coordinates are Y-up. Absorb that backend
    // detail here so layout/widgets never need to care about reverse viewport.
    float viewportY      = 0.0f;
    float viewportHeight = static_cast<float>(height);
    if (render && render->getAPI() == ERenderAPI::Vulkan) {
        viewportY      = static_cast<float>(height);
        viewportHeight = -static_cast<float>(height);
    }
    cmdBuf.setViewport(0.0f, viewportY, static_cast<float>(width), viewportHeight, 0.0f, 1.0f);
    cmdBuf.setScissor(0, 0, width, height);
}

void setWorldViewportAndScissor(ICommandBuffer& cmdBuf, IRender* render, uint32_t width, uint32_t height)
{
    float viewportY      = 0.0f;
    float viewportHeight = static_cast<float>(height);
    if (shouldReverseWorldViewport(render)) {
        viewportY      = static_cast<float>(height);
        viewportHeight = -static_cast<float>(height);
    }
    cmdBuf.setViewport(0.0f, viewportY, static_cast<float>(width), viewportHeight, 0.0f, 1.0f);
    cmdBuf.setScissor(0, 0, width, height);
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
    session.view          = ctx.view;
    session.viewProjection = ctx.viewProjection;
    session.windowHeight  = ctx.windowHeight;
    session.windowWidth   = ctx.windowWidth;
    session.passSlot      = ctx.passSlot;
    session.clipStack.clear();
    Extent2D extent{.width = session.windowWidth, .height = session.windowHeight};
    quadData->begin(ctx.passSlot, extent);
    lineData->begin(ctx.passSlot);
}

void Render2D::end()
{
    quadData->end();
    lineData->flush(session.curCmdBuf, session.viewProjection);

    session.curCmdBuf    = nullptr;
    session.windowWidth  = 0;
    session.windowHeight = 0;
}

Render2DPassSlot Render2D::acquirePassSlot()
{
    static Render2DPassSlot sNextSlot = 0;
    const Render2DPassSlot  slot      = sNextSlot++;
    YA_CORE_ASSERT(slot < FQuadRender::kMaxPassSlots,
                   "Render2D pass slot pool exhausted ({} slots)",
                   FQuadRender::kMaxPassSlots);
    return slot;
}

void Render2D::preparePassPipeline(Render2DPassSlot passSlot, EFormat::T colorFormat, EFormat::T depthFormat)
{
    if (quadData) {
        quadData->preparePassPipeline(passSlot, colorFormat, depthFormat);
    }
}

void Render2D::pushClipRect(const Rect2D& rect)
{
    // Intersect with the current clip so nested clips never exceed their parent.
    Rect2D clipped = rect;
    if (!session.clipStack.empty()) {
        clipped = intersectClipRect(rect, session.clipStack.back());
    }

    const bool bClipChanged = session.clipStack.empty() ||
                              session.clipStack.back().pos != clipped.pos ||
                              session.clipStack.back().extent != clipped.extent;
    if (bClipChanged && quadData && session.curCmdBuf) {
        // Flush pending quads with the CURRENT scissor BEFORE switching to the
        // new clip; otherwise content recorded outside the clip gets culled by
        // the incoming clip rect.
        quadData->flush(session.curCmdBuf);
    }
    session.clipStack.push_back(clipped);
}

Rect2D Render2D::intersectClipRect(const Rect2D& rect, const Rect2D& parentClip)
{
    const glm::vec2 parentMax = parentClip.pos + parentClip.extent;
    const glm::vec2 rectMax   = rect.pos + rect.extent;
    const glm::vec2 clippedPos    = glm::max(rect.pos, parentClip.pos);
    const glm::vec2 clippedExtent = glm::max(glm::vec2(0.0f), glm::min(rectMax, parentMax) - clippedPos);
    return Rect2D{.pos = clippedPos, .extent = clippedExtent};
}

void Render2D::popClipRect()
{
    if (session.clipStack.empty()) {
        return;
    }
    if (quadData && session.curCmdBuf) {
        // Flush pending quads with the CURRENT (inner) scissor BEFORE popping;
        // otherwise content recorded inside the clip escapes it.
        quadData->flush(session.curCmdBuf);
    }
    session.clipStack.pop_back();
}

void FLineRender::init(IRender* render, EFormat::T colorFormat, EFormat::T depthFormat)
{
    _render = render;
    constexpr uint32_t resourceCount = FQuadRender::kMaxPassSlots * MAX_FLIGHTS_IN_FLIGHT;

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

void FLineRender::ensureSlotResources(Render2DPassSlot passSlot)
{
    auto& slot = _passResources[static_cast<size_t>(passSlot)];
    if (slot.flights[0].frameUBOBuffer) {
        return; // already allocated
    }
    std::vector<DescriptorSetHandle> descriptorSets;
    _descriptorPool->allocateDescriptorSets(_frameUboDSL, MAX_FLIGHTS_IN_FLIGHT, descriptorSets);
    for (uint32_t flight = 0; flight < MAX_FLIGHTS_IN_FLIGHT; ++flight) {
        auto& resources = slot.flights[flight];
        resources.frameUboDS = descriptorSets[flight];
        resources.frameUBOBuffer = _render->getResourceFactory()->createBuffer(
            ya::BufferCreateInfo{
                .label       = std::format("Sprite2D_Line_{}_{}_FrameUBO", passSlot, flight),
                .usage       = EBufferUsage::UniformBuffer,
                .size        = sizeof(FrameUBO),
                .memoryUsage = EMemoryUsage::CpuToGpu,
            });
        resources.vertexBuffer = _render->getResourceFactory()->createBuffer(
            ya::BufferCreateInfo{
                .label       = std::format("Sprite2D_Line_{}_{}_VertexBuffer", passSlot, flight),
                .usage       = EBufferUsage::VertexBuffer | EBufferUsage::TransferDst,
                .size        = sizeof(FLineRender::Vertex) * MaxVertexCount * kFrameFlushSlots,
                .memoryUsage = EMemoryUsage::CpuToGpu,
            });
        resources.vertexPtrHead = resources.vertexBuffer->map<FLineRender::Vertex>();
    }
}

void FLineRender::begin(Render2DPassSlot passSlot)
{
    _activePassSlot  = passSlot;
    _activeFlightIndex = _render ? _render->getCurrentFrameIndex() % MAX_FLIGHTS_IN_FLIGHT : 0;
    ensureSlotResources(passSlot);
    auto& resources    = _passResources[static_cast<size_t>(_activePassSlot)].flights[_activeFlightIndex];
    vertexPtrHead      = resources.vertexPtrHead;
    vertexPtr   = vertexPtrHead;
    vertexCount = 0;
    batchStartVertex = 0;
}

void FLineRender::flush(ICommandBuffer* cmdBuf, const glm::mat4& viewProj)
{
    if (!cmdBuf || vertexCount == 0) {
        return;
    }

    FrameUBO ubo{
        .viewProj = viewProj,
        .view     = Render2D::session.view,
    };
    auto& resources = _passResources[static_cast<size_t>(_activePassSlot)].flights[_activeFlightIndex];
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
    setScreenViewportAndScissor(*cmdBuf, _render, Render2D::session.windowWidth, Render2D::session.windowHeight);

    cmdBuf->bindDescriptorSets(_pipelineLayout.get(), 0, {resources.frameUboDS});
    cmdBuf->bindVertexBuffer(0, resources.vertexBuffer.get(), 0);
    YA_CORE_ASSERT(static_cast<uint64_t>(batchStartVertex) + vertexCount <=
                       MaxVertexCount * kFrameFlushSlots,
                   "Render2D line frame exceeded vertex buffer capacity ({} batches)",
                   kFrameFlushSlots);
    cmdBuf->draw(vertexCount, 1, batchStartVertex, 0);

    batchStartVertex = static_cast<uint32_t>(vertexPtr - vertexPtrHead);
    vertexCount = 0;
}

void FLineRender::addLine(const glm::vec3& from, const glm::vec3& to, const glm::vec4& color)
{
    if (vertexCount + 2 > MaxVertexCount) {
        flush(Render2D::session.curCmdBuf, Render2D::session.viewProjection);
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
    constexpr uint32_t slotCount        = kMaxPassSlots;
    constexpr uint32_t frameResourceCount = slotCount * MAX_FLIGHTS_IN_FLIGHT;
    constexpr uint32_t imageResourceCount = slotCount * MAX_FLIGHTS_IN_FLIGHT * RESOURCE_DS_POOL_SIZE;

    // Shared descriptor pool sized for the worst case (all slots in use).
    // Per-slot sets are allocated lazily by ensureSlotResources, so a GUI app
    // that uses one slot never touches the rest of the pool.
    _descriptorPool = IDescriptorPool::create(
        render,
        DescriptorPoolCreateInfo{
            .maxSets   = frameResourceCount * 2 + imageResourceCount * 2,
            .poolSizes = {
                DescriptorPoolSize{
                    .type            = EPipelineDescriptorType::UniformBuffer,
                    .descriptorCount = frameResourceCount * 2,
                },
                DescriptorPoolSize{
                    .type            = EPipelineDescriptorType::CombinedImageSampler,
                    .descriptorCount = imageResourceCount * TEXTURE_SET_SIZE * 2,
                },
            },
        });

    _frameUboDSL = IDescriptorSetLayout::create(render, _pipelineDesc.descriptorSetLayouts[0]);
    _resourceDSL = IDescriptorSetLayout::create(render, _pipelineDesc.descriptorSetLayouts[1]);

    std::vector<std::shared_ptr<IDescriptorSetLayout>> dslVec = {_frameUboDSL, _resourceDSL};
    _pipelineLayout = IPipelineLayout::create(render, "Sprite2D_PipelineLayout", _pipelineDesc.pushConstants, dslVec);

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
            resources.screenResourceDSPool.clear();
            resources.worldResourceDSPool.clear();
            resources.activeScreenResourceDS = {};
            resources.activeWorldResourceDS  = {};
            resources.nextScreenResourceDS   = 0;
            resources.nextWorldResourceDS    = 0;
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

void FQuadRender::preparePassPipeline(Render2DPassSlot passSlot, EFormat::T colorFormat, EFormat::T depthFormat)
{
    if (!_render || colorFormat == EFormat::Undefined) {
        return;
    }

    auto& pipelines = _passPipelines[static_cast<size_t>(passSlot)];
    if (depthFormat == EFormat::Undefined) {
        // Depth-less target (UI composite / editor canvas): use the depth-less
        // UI variant. The decision follows the actual attachment, not a
        // hardcoded pass vocabulary.
        if (pipelines.uiPipeline && pipelines.uiColorFormat == colorFormat) {
            return;
        }

        auto pipeline = IGraphicsPipeline::create(_render);
        pipeline->recreate(buildQuadScreenPipelineCI(_pipelineLayout.get(),
                                                     std::format("Sprite2D_{}_UI_Pipeline", passSlot),
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
                                                 std::format("Sprite2D_{}_Screen_Pipeline", passSlot),
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

void FQuadRender::ensureSlotResources(Render2DPassSlot passSlot)
{
    auto& slot = _passResources[static_cast<size_t>(passSlot)];
    if (slot.flights[0].frameUBOBuffer) {
        return; // already allocated
    }

    // Frame UBO descriptor sets + buffers (screen + world) for all flights.
    std::vector<ya::DescriptorSetHandle> descriptorSets;
    _descriptorPool->allocateDescriptorSets(_frameUboDSL, MAX_FLIGHTS_IN_FLIGHT * 2, descriptorSets);
    for (uint32_t flight = 0; flight < MAX_FLIGHTS_IN_FLIGHT; ++flight) {
        auto& resources = slot.flights[flight];
        resources.frameUboDS      = descriptorSets[flight * 2];
        resources.worldFrameUboDS = descriptorSets[flight * 2 + 1];
        resources.frameUBOBuffer = _render->getResourceFactory()->createBuffer(
            ya::BufferCreateInfo{
                .label       = std::format("Sprite2D_{}_{}_FrameUBO", passSlot, flight),
                .usage       = EBufferUsage::UniformBuffer,
                .size        = sizeof(FrameUBO),
                .memoryUsage = EMemoryUsage::CpuToGpu,
            });
        resources.worldFrameUBOBuffer = _render->getResourceFactory()->createBuffer(
            ya::BufferCreateInfo{
                .label       = std::format("Sprite2D_{}_{}_WorldFrameUBO", passSlot, flight),
                .usage       = EBufferUsage::UniformBuffer,
                .size        = sizeof(FrameUBO),
                .memoryUsage = EMemoryUsage::CpuToGpu,
            });
    }

    // Texture-array resource descriptor sets (screen + world pools).
    descriptorSets.clear();
    _descriptorPool->allocateDescriptorSets(
        _resourceDSL,
        MAX_FLIGHTS_IN_FLIGHT * RESOURCE_DS_POOL_SIZE * 2,
        descriptorSets);
    for (uint32_t flight = 0; flight < MAX_FLIGHTS_IN_FLIGHT; ++flight) {
        auto& resources = slot.flights[flight];
        resources.screenResourceDSPool.reserve(RESOURCE_DS_POOL_SIZE);
        resources.worldResourceDSPool.reserve(RESOURCE_DS_POOL_SIZE);
        for (uint32_t i = 0; i < RESOURCE_DS_POOL_SIZE; ++i) {
            const size_t base = static_cast<size_t>(flight) * RESOURCE_DS_POOL_SIZE * 2;
            resources.screenResourceDSPool.push_back(descriptorSets[base + i * 2]);
            resources.worldResourceDSPool.push_back(descriptorSets[base + i * 2 + 1]);
        }
    }

    // Host-visible vertex buffers (screen + world) for all flights.
    for (uint32_t flight = 0; flight < MAX_FLIGHTS_IN_FLIGHT; ++flight) {
        auto& resources = slot.flights[flight];
        resources.vertexBuffer = _render->getResourceFactory()->createBuffer(
            ya::BufferCreateInfo{
                .label       = std::format("Sprite2D_{}_{}_Screen_VertexBuffer", passSlot, flight),
                .usage       = EBufferUsage::VertexBuffer | EBufferUsage::TransferDst,
                .size        = sizeof(FQuadRender::Vertex) * MaxVertexCount * kFrameFlushSlots,
                .memoryUsage = EMemoryUsage::CpuToGpu,
            });
        resources.vertexPtrHead = resources.vertexBuffer->map<FQuadRender::Vertex>();

        resources.worldVertexBuffer = _render->getResourceFactory()->createBuffer(
            ya::BufferCreateInfo{
                .label       = std::format("Sprite2D_{}_{}_World_VertexBuffer", passSlot, flight),
                .usage       = EBufferUsage::VertexBuffer | EBufferUsage::TransferDst,
                .size        = sizeof(FQuadRender::Vertex) * MaxVertexCount * kFrameFlushSlots,
                .memoryUsage = EMemoryUsage::CpuToGpu,
            });
        resources.worldVertexPtrHead = resources.worldVertexBuffer->map<FQuadRender::Vertex>();
    }
}

void FQuadRender::begin(Render2DPassSlot passSlot, const Extent2D& extent)
{
    _activePassSlot = passSlot;
    _activeFlightIndex = _render ? _render->getCurrentFrameIndex() % MAX_FLIGHTS_IN_FLIGHT : 0;
    ensureSlotResources(passSlot);
    auto& resources = activeFlightResources();
    vertexPtrHead      = resources.vertexPtrHead;
    vertexPtr          = vertexPtrHead;
    worldVertexPtrHead = resources.worldVertexPtrHead;
    worldVertexPtr     = worldVertexPtrHead;
    vertexCount        = 0;
    indexCount         = 0;
    worldVertexCount   = 0;
    worldIndexCount    = 0;
    screenBatchStartVertex = 0;
    worldBatchStartVertex  = 0;
    _resourceVersion                    = 1;
    _uploadedScreenResourceVersion      = 0;
    _uploadedWorldResourceVersion       = 0;
    _frameUboUploaded                   = false;
    _worldFrameUboUploaded              = false;
    resources.activeScreenResourceDS    = {};
    resources.activeWorldResourceDS     = {};
    resources.nextScreenResourceDS      = 0;
    resources.nextWorldResourceDS       = 0;
    resetTextureBatch();

    float w      = static_cast<float>(extent.width);
    float h      = static_cast<float>(extent.height);
    float aspect = w / h;
    if (w > h) {
        h = w / aspect;
    }
    else {
        w = h * aspect;
    }

    // Screen-space UI owns a top-left / Y-down app-space contract.
    // For the current Vulkan path we keep the projection top-down and also
    // use a negative-height viewport so clip-space and framebuffer-space stay
    // aligned with the rest of Render2D's screen-space assumptions.
    _screenOrthoProj = glm::orthoRH_ZO(0.0f, w, h, 0.0f, -1.0f, 1.0f);
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
    // The screen pipeline variant was chosen at prep time by the target
    // attachment: depth-less targets resolved to uiPipeline, depth-attached
    // targets to screenPipeline. Prefer whichever was prepared for this slot.
    IGraphicsPipeline* pipeline = pipelines.screenPipeline ? pipelines.screenPipeline.get()
                                                           : (pipelines.uiPipeline ? pipelines.uiPipeline.get() : nullptr);
    YA_CORE_ASSERT(pipeline != nullptr,
                   "Render2D pipeline for pass slot {} was not prepared before command recording",
                   static_cast<size_t>(_activePassSlot));
    cmdBuf->bindPipeline(pipeline);
    setScreenViewportAndScissor(*cmdBuf, _render, Render2D::session.windowWidth, Render2D::session.windowHeight);
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
        resources.activeScreenResourceDS = acquireScreenResourceDS(resources);
        updateResources(resources.activeScreenResourceDS);
        _uploadedScreenResourceVersion = _resourceVersion;
    }
    if (!_frameUboUploaded) {
        updateFrameUBO(resources.frameUBOBuffer, resources.frameUboDS, _screenOrthoProj, glm::mat4(1.0f));
        _frameUboUploaded = true;
    }

    // The shared host-visible buffer is written during recording but read by
    // the GPU only after submission, so every batch must live at a distinct
    // offset (see kFrameFlushSlots). Draw the pending batch at its recorded
    // region instead of always starting at vertex 0.
    YA_CORE_ASSERT(static_cast<uint64_t>(screenBatchStartVertex) + vertexCount <=
                       MaxVertexCount * kFrameFlushSlots,
                   "Render2D screen frame exceeded vertex buffer capacity ({} batches)",
                   kFrameFlushSlots);
    const std::vector<DescriptorSetHandle> descriptorSets = {
        resources.frameUboDS,
        resources.activeScreenResourceDS,
    };
    cmdBuf->bindDescriptorSets(_pipelineLayout.get(), 0, descriptorSets);
    cmdBuf->bindVertexBuffer(0, resources.vertexBuffer.get(), 0);
    cmdBuf->bindIndexBuffer(_indexBuffer.get(), 0, false);
    cmdBuf->drawIndexed(static_cast<uint32_t>(indexCount), 1, 0, static_cast<int32_t>(screenBatchStartVertex), 0);

    screenBatchStartVertex = static_cast<uint32_t>(vertexPtr - vertexPtrHead);
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
        resources.activeWorldResourceDS = acquireWorldResourceDS(resources);
        updateResources(resources.activeWorldResourceDS);
        _uploadedWorldResourceVersion = _resourceVersion;
    }
    if (!_worldFrameUboUploaded) {
        updateFrameUBO(resources.worldFrameUBOBuffer,
                       resources.worldFrameUboDS,
                       Render2D::session.viewProjection,
                       Render2D::session.view);
        _worldFrameUboUploaded = true;
    }
    resources.worldVertexBuffer->flush();

    cmdBuf->bindPipeline(_worldPipeline.get());
    setWorldViewportAndScissor(*cmdBuf, _render, Render2D::session.windowWidth, Render2D::session.windowHeight);
    cmdBuf->setCullMode(Render2D::debug.worldCullMode);

    YA_CORE_ASSERT(static_cast<uint64_t>(worldBatchStartVertex) + worldVertexCount <=
                       MaxVertexCount * kFrameFlushSlots,
                   "Render2D world frame exceeded vertex buffer capacity ({} batches)",
                   kFrameFlushSlots);
    std::vector<DescriptorSetHandle> descriptorSets = {
        resources.worldFrameUboDS,
        resources.activeWorldResourceDS,
    };
    cmdBuf->bindDescriptorSets(_pipelineLayout.get(), 0, descriptorSets);
    cmdBuf->bindVertexBuffer(0, resources.worldVertexBuffer.get(), 0);
    cmdBuf->bindIndexBuffer(_indexBuffer.get(), 0, false);
    cmdBuf->drawIndexed(static_cast<uint32_t>(worldIndexCount), 1, 0, static_cast<int32_t>(worldBatchStartVertex), 0);

    worldBatchStartVertex = static_cast<uint32_t>(worldVertexPtr - worldVertexPtrHead);
    worldVertexCount = 0;
    worldIndexCount  = 0;
}

void FQuadRender::resetTextureBatch()
{
    _textureBindings.clear();
    _textureLabel2Idx.clear();
    _textureBindings.push_back(TextureBinding{
        .texture = TextureLibrary::get().getWhiteTexture(),
        .sampler = TextureLibrary::get().getDefaultSampler(),
    });
    _lastPushTextureSlot              = static_cast<int>(_textureBindings.size() - 1);
    _resourceVersion                  = std::max<uint64_t>(_resourceVersion + 1, 1);
    _uploadedScreenResourceVersion    = 0;
    _uploadedWorldResourceVersion     = 0;
}

void FQuadRender::flushForTextureOverflow(ICommandBuffer* cmdBuf)
{
    flushWorld(cmdBuf);
    flush(cmdBuf);
    resetTextureBatch();
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

DescriptorSetHandle FQuadRender::acquireScreenResourceDS(FlightResources& resources)
{
    YA_CORE_ASSERT(resources.nextScreenResourceDS < resources.screenResourceDSPool.size(),
                   "Render2D exhausted screen resource descriptor sets for pass slot {} flight {}",
                   static_cast<uint32_t>(_activePassSlot),
                   _activeFlightIndex);
    return resources.screenResourceDSPool[resources.nextScreenResourceDS++];
}

DescriptorSetHandle FQuadRender::acquireWorldResourceDS(FlightResources& resources)
{
    YA_CORE_ASSERT(resources.nextWorldResourceDS < resources.worldResourceDSPool.size(),
                   "Render2D exhausted world resource descriptor sets for pass slot {} flight {}",
                   static_cast<uint32_t>(_activePassSlot),
                   _activeFlightIndex);
    return resources.worldResourceDSPool[resources.nextWorldResourceDS++];
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
            if (_textureBindings.size() >= TEXTURE_SET_SIZE) {
                flushForTextureOverflow(Render2D::session.curCmdBuf);
            }
            _textureBindings.push_back(TextureBinding{
                .texture = texture,
                .sampler = resolveSamplerForTexture(texture.get()),
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
    if (vertexCount >= MaxVertexCount - 4) {
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
    if (vertexCount >= MaxVertexCount - 4) {
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
    if (worldVertexCount >= MaxVertexCount - 4) {
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
    if (vertexCount >= MaxVertexCount - 4) {
        flush(Render2D::session.curCmdBuf);
    }

    uint32_t textureIdx = findOrAddTexture(texture);

    glm::mat4 model = glm::translate(glm::mat4(1.f), {position.x, position.y, position.z}) *
                      glm::scale(glm::mat4(1.f), glm::vec3(size, 1.0f));

    drawTextureInternal(model, textureIdx, tint, {uvRect.z, uvRect.w}, {uvRect.x, uvRect.y});
}

void FQuadRender::drawText(const std::string& text,
                           const glm::vec3&   position,
                           const glm::vec4&   color,
                           Font*              font,
                           const glm::vec2&   scale)
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
            cursorY += font->lineHeight * scale.y;
            continue;
        }

        const Character& character = font->getCharacter(codePoint);
        if (codePoint == ' ') {
            cursorX += character.advance.x * scale.x;
            continue;
        }
        if (codePoint == '\t') {
            cursorX += font->getCharacter(' ').advance.x * 4.0f * scale.x;
            continue;
        }

        float xpos = cursorX + static_cast<float>(character.bearing.x) * scale.x;
        float ypos = cursorY + static_cast<float>(font->ascent - character.bearing.y) * scale.y;
        glm::vec3 pos  = glm::vec3(xpos, ypos, position.z);
        const glm::vec2 scaledGlyphSize = glm::vec2(character.size) * scale;

        if (!character.bInAtlas) {
            if (character.standaloneTexture) {
                drawTexture(pos, scaledGlyphSize, character.standaloneTexture, color);
            }
        }
        else {
            drawSubTexture(pos,
                           scaledGlyphSize,
                           font->atlasTexture,
                           color,
                           character.uvRect);
        }

        cursorX += character.advance.x * scale.x;
    }
}

} // namespace ya
