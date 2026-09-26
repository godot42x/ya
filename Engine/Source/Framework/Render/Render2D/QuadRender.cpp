#include "Draw2DInternal.h"
#include "Render2D/Render2D.h"

#include "RHI/Core/CommandBuffer.h"
#include "RHI/Core/RenderPass.h"
#include "RHI/Core/RenderResourceFactory.h"
#include "RHI/Render.h"
#include "RHI/RenderDefines.h"

#include "Core/Common/DeferredDeletionQueue.h"
#include "Core/Log.h"
#include "Core/Math/GLM.h"
#include "Render/Resources/FontManager.h"
#include "RHI/Backend/TextureLibrary.h"

#include "utility.cc/ranges.h"

#include <algorithm>
#include <array>
#include <format>
#include <limits>

namespace ya
{

namespace
{

bool shouldLogFlush(uint32_t& counter)
{
    if (!Render2D::debug.bLogFlushBatches) {
        return false;
    }
    if (counter >= Render2D::debug.maxFlushLogsPerFrame) {
        return false;
    }
    ++counter;
    return true;
}

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

ViewportState buildQuadViewportState()
{
    // The pipelines' viewport/scissor are DYNAMIC state (see dynamicFeatures):
    // this CI placeholder is overridden by setScreenViewportAndScissor on
    // every flush, so it carries a fixed legal value instead of reading a
    // recording's extent.
    return ViewportState{
        .viewports = {Viewport{
            .x        = 0.0f,
            .y        = 0.0f,
            .width    = 1.0f,
            .height   = 1.0f,
            .minDepth = 0.0f,
            .maxDepth = 1.0f,
        }},
        .scissors = {Scissor{
            .offsetX = 0,
            .offsetY = 0,
            .width   = 1,
            .height  = 1,
        }},
    };
}

namespace
{

ya::Ptr<Sampler> resolveSamplerForTexture(Texture* texture)
{
    if (!texture) {
        return TextureLibrary::get().getDefaultSampler();
    }

    // Sampler category is declared once at atlas creation (DynamicFontAtlas::
    // replaceTexture), NOT string-matched here on the hot draw path.
    switch (texture->getSamplerCategory()) {
        case ESamplerCategory::ClampLinear:
            return TextureLibrary::get().getClampLinearSampler();
        case ESamplerCategory::ClampNearest:
            return TextureLibrary::get().getClampNearestSampler();
        case ESamplerCategory::Default:
        default:
            return TextureLibrary::get().getDefaultSampler();
    }
}

bool shouldReverseWorldViewport(IRender* render)
{
    if (!Render2D::debug.bReverseViewport) {
        return false;
    }
    return render && render->getAPI() == ERenderAPI::Vulkan;
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
            .offset     = offsetof(FQuadRender::Vertex, textureRef),
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
        VertexAttribute{
            .bufferSlot = 0,
            .location   = 7,
            .format     = EVertexAttributeFormat::Float3,
            .offset     = offsetof(FQuadRender::Vertex, corner),
        },
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

GraphicsPipelineCreateInfo buildQuadWorldPipelineCI(IPipelineLayout* pipelineLayout,
                                                    EFormat::T colorFormat,
                                                    EFormat::T depthFormat)
{
    return GraphicsPipelineCreateInfo{
        .subPassRef            = 0,
        .renderPass            = nullptr,
        .pipelineRenderingInfo = PipelineRenderingInfo{
            .label                  = "Sprite2D_World_Pipeline",
            .viewMask               = 0,
            .colorAttachmentFormats = {colorFormat},
            .depthAttachmentFormat  = depthFormat,
        },
        .pipelineLayout = pipelineLayout,
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

void ensureWorldPipeline(FQuadRender& quad,
                         IRender* render,
                         IPipelineLayout* pipelineLayout,
                         EFormat::T colorFormat,
                         EFormat::T depthFormat)
{
    if (!render || !pipelineLayout || colorFormat == EFormat::Undefined || depthFormat == EFormat::Undefined) {
        return;
    }
    if (quad._worldPipeline &&
        quad._worldColorFormat == colorFormat &&
        quad._worldDepthFormat == depthFormat) {
        return;
    }

    auto pipeline = IGraphicsPipeline::create(render);
    pipeline->recreate(buildQuadWorldPipelineCI(pipelineLayout, colorFormat, depthFormat));
    auto retired = std::move(quad._worldPipeline);
    quad._worldPipeline     = std::move(pipeline);
    quad._worldColorFormat  = colorFormat;
    quad._worldDepthFormat  = depthFormat;
    DeferredDeletionQueue::get().retire(std::move(retired));
}

} // namespace

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

    ensureWorldPipeline(*this, render, _pipelineLayout.get(), colorFormat, depthFormat);

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
    _worldColorFormat = EFormat::Undefined;
    _worldDepthFormat = EFormat::Undefined;
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
        DeferredDeletionQueue::get().retire(std::move(retired));
        return;
    }

    if (!(pipelines.screenPipeline &&
          pipelines.screenColorFormat == colorFormat &&
          pipelines.screenDepthFormat == depthFormat)) {
        auto pipeline = IGraphicsPipeline::create(_render);
        pipeline->recreate(buildQuadScreenPipelineCI(_pipelineLayout.get(),
                                                     std::format("Sprite2D_{}_Screen_Pipeline", passSlot),
                                                     colorFormat,
                                                     depthFormat));
        auto retired = std::move(pipelines.screenPipeline);
        pipelines.screenPipeline = std::move(pipeline);
        pipelines.screenColorFormat = colorFormat;
        pipelines.screenDepthFormat = depthFormat;
        DeferredDeletionQueue::get().retire(std::move(retired));
    }

    // Shared world sprites (overlay billboards, editor gizmos) key off the
    // last depth-attached target. Runtime UI's depth-less prep must not
    // clobber this; Deferred vs Forward depth must rebuild it.
    ensureWorldPipeline(*this, _render, _pipelineLayout.get(), colorFormat, depthFormat);
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
        // Bind the persistent host-visible UBO once at allocation. Later
        // flushes only writeData(); vkUpdateDescriptorSets after the set is
        // bound in a recording command buffer invalidates that region
        // (no UPDATE_AFTER_BIND).
        _render->getDescriptorHelper()->updateDescriptorSets({
            IDescriptorSetHelper::writeOneUniformBuffer(resources.frameUboDS, 0, resources.frameUBOBuffer.get()),
            IDescriptorSetHelper::writeOneUniformBuffer(resources.worldFrameUboDS, 0, resources.worldFrameUBOBuffer.get()),
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

void FQuadRender::begin(Render2DPassSlot passSlot, const Extent2D& extent, uint32_t flightSlot)
{
    _activePassSlot = passSlot;
    _activeFlightIndex = flightSlot % MAX_FLIGHTS_IN_FLIGHT;
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

void FQuadRender::flush(ICommandBuffer* cmdBuf, const FRender2dFlushState& state)
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
    setScreenViewportAndScissor(*cmdBuf, _render, state.windowWidth, state.windowHeight);
    // Defensive: clamp the clip-derived scissor to the window bounds. Layout
    // already keeps rects non-negative, but a stale clip must never cast a
    // negative extent into a uint32 (VUID offset+extent overflow).
    if (state.bClipped) {
        const Rect2D& clip = state.clip;
        const int32_t sx = std::clamp(static_cast<int32_t>(clip.pos.x), 0, static_cast<int32_t>(state.windowWidth));
        const int32_t sy = std::clamp(static_cast<int32_t>(clip.pos.y), 0, static_cast<int32_t>(state.windowHeight));
        const int32_t sw = std::clamp(static_cast<int32_t>(clip.extent.x), 0,
                                      static_cast<int32_t>(state.windowWidth) - sx);
        const int32_t sh = std::clamp(static_cast<int32_t>(clip.extent.y), 0,
                                      static_cast<int32_t>(state.windowHeight) - sy);
        cmdBuf->setScissor(sx, sy, static_cast<uint32_t>(sw), static_cast<uint32_t>(sh));
    }
    else {
        cmdBuf->setScissor(0, 0, state.windowWidth, state.windowHeight);
    }
    if (_render && _render->getCapabilities().dynamicCullMode) {
        cmdBuf->setCullMode(Render2D::debug.screenCullMode);
    }

    if (_uploadedScreenResourceVersion != _resourceVersion) {
        resources.activeScreenResourceDS = acquireScreenResourceDS(resources);
        updateResources(resources.activeScreenResourceDS);
        _uploadedScreenResourceVersion = _resourceVersion;
    }
    if (!_frameUboUploaded) {
        updateFrameUBO(resources.frameUBOBuffer, _screenOrthoProj, glm::mat4(1.0f));
        _frameUboUploaded = true;
    }

    // The shared host-visible buffer is written during recording but read by
    // the GPU only after submission, so every batch must live at a distinct
    // offset (see kFrameFlushSlots). Draw the pending batch at its recorded
    // region instead of always starting at vertex 0.
    const uint32_t cursorVertex = static_cast<uint32_t>(vertexPtr - vertexPtrHead);
    YA_CORE_ASSERT(cursorVertex == screenBatchStartVertex + vertexCount,
                   "Render2D screen batch cursor mismatch: startVertex={} vertexCount={} cursorVertex={}",
                   screenBatchStartVertex,
                   vertexCount,
                   cursorVertex);
    YA_CORE_ASSERT(static_cast<uint64_t>(screenBatchStartVertex) + vertexCount <=
                       MaxVertexCount * kFrameFlushSlots,
                   "Render2D screen frame exceeded vertex buffer capacity ({} batches)",
                   kFrameFlushSlots);
    if (state.debugScreenFlushCount && shouldLogFlush(*state.debugScreenFlushCount)) {
        int32_t clipX = 0;
        int32_t clipY = 0;
        uint32_t clipW = state.windowWidth;
        uint32_t clipH = state.windowHeight;
        if (state.bClipped) {
            const Rect2D& clip = state.clip;
            clipX = std::clamp(static_cast<int32_t>(clip.pos.x), 0, static_cast<int32_t>(state.windowWidth));
            clipY = std::clamp(static_cast<int32_t>(clip.pos.y), 0, static_cast<int32_t>(state.windowHeight));
            clipW = static_cast<uint32_t>(std::clamp(static_cast<int32_t>(clip.extent.x), 0,
                                                     static_cast<int32_t>(state.windowWidth) - clipX));
            clipH = static_cast<uint32_t>(std::clamp(static_cast<int32_t>(clip.extent.y), 0,
                                                     static_cast<int32_t>(state.windowHeight) - clipY));
        }
        YA_CORE_INFO("Render2D screen flush: passSlot={} flight={} batch={} clip=({}, {}, {}, {}) startVertex={} cursorVertex={} vertexCount={} indexCount={} resourceVersion={} uploadedResourceVersion={} textures={}",
                     static_cast<size_t>(_activePassSlot),
                     _activeFlightIndex,
                     state.stats ? state.stats->screenFlushCount : 0u,
                     clipX,
                     clipY,
                     clipW,
                     clipH,
                     screenBatchStartVertex,
                     cursorVertex,
                     vertexCount,
                     indexCount,
                     _resourceVersion,
                     _uploadedScreenResourceVersion,
                     _textureBindings.size());
    }
    const std::vector<DescriptorSetHandle> descriptorSets = {
        resources.frameUboDS,
        resources.activeScreenResourceDS,
    };
    cmdBuf->bindDescriptorSets(_pipelineLayout.get(), 0, descriptorSets);
    cmdBuf->bindVertexBuffer(0, resources.vertexBuffer.get(), 0);
    cmdBuf->bindIndexBuffer(_indexBuffer.get(), 0, false);
    cmdBuf->drawIndexed(static_cast<uint32_t>(indexCount), 1, 0, static_cast<int32_t>(screenBatchStartVertex), 0);

    if (state.stats) {
        ++state.stats->screenFlushCount;
        state.stats->screenVertexCount += vertexCount;
        state.stats->screenIndexCount += static_cast<uint32_t>(indexCount);
    }
    if (state.debugScreenFlushCount) {
        ++*state.debugScreenFlushCount;
    }

    screenBatchStartVertex = static_cast<uint32_t>(vertexPtr - vertexPtrHead);
    vertexCount = 0;
    indexCount  = 0;
}

void FQuadRender::flushWorld(ICommandBuffer* cmdBuf, const FRender2dFlushState& state)
{
    if (!cmdBuf || worldVertexCount == 0) {
        return;
    }

    auto& resources = activeFlightResources();
    if (_uploadedWorldResourceVersion != _resourceVersion) {
        resources.activeWorldResourceDS = acquireWorldResourceDS(resources);
        updateResources(resources.activeWorldResourceDS);
        _uploadedWorldResourceVersion = _resourceVersion;
    }
    if (!_worldFrameUboUploaded) {
        updateFrameUBO(resources.worldFrameUBOBuffer, state.viewProjection, state.view);
        _worldFrameUboUploaded = true;
    }
    resources.worldVertexBuffer->flush();

    YA_CORE_ASSERT(_worldPipeline != nullptr,
                   "Render2D world pipeline was not prepared before command recording");
    cmdBuf->bindPipeline(_worldPipeline.get());
    setWorldViewportAndScissor(*cmdBuf, _render, state.windowWidth, state.windowHeight);
    if (_render && _render->getCapabilities().dynamicCullMode) {
        cmdBuf->setCullMode(Render2D::debug.worldCullMode);
    }

    const uint32_t cursorVertex = static_cast<uint32_t>(worldVertexPtr - worldVertexPtrHead);
    YA_CORE_ASSERT(cursorVertex == worldBatchStartVertex + worldVertexCount,
                   "Render2D world batch cursor mismatch: startVertex={} vertexCount={} cursorVertex={}",
                   worldBatchStartVertex,
                   worldVertexCount,
                   cursorVertex);
    YA_CORE_ASSERT(static_cast<uint64_t>(worldBatchStartVertex) + worldVertexCount <=
                       MaxVertexCount * kFrameFlushSlots,
                   "Render2D world frame exceeded vertex buffer capacity ({} batches)",
                   kFrameFlushSlots);
    if (state.debugWorldFlushCount && shouldLogFlush(*state.debugWorldFlushCount)) {
        YA_CORE_INFO("Render2D world flush: passSlot={} flight={} batch={} startVertex={} cursorVertex={} vertexCount={} indexCount={} resourceVersion={} uploadedResourceVersion={} textures={}",
                     static_cast<size_t>(_activePassSlot),
                     _activeFlightIndex,
                     state.stats ? state.stats->worldFlushCount : 0u,
                     worldBatchStartVertex,
                     cursorVertex,
                     worldVertexCount,
                     worldIndexCount,
                     _resourceVersion,
                     _uploadedWorldResourceVersion,
                     _textureBindings.size());
    }
    std::vector<DescriptorSetHandle> descriptorSets = {
        resources.worldFrameUboDS,
        resources.activeWorldResourceDS,
    };
    cmdBuf->bindDescriptorSets(_pipelineLayout.get(), 0, descriptorSets);
    cmdBuf->bindVertexBuffer(0, resources.worldVertexBuffer.get(), 0);
    cmdBuf->bindIndexBuffer(_indexBuffer.get(), 0, false);
    cmdBuf->drawIndexed(static_cast<uint32_t>(worldIndexCount), 1, 0, static_cast<int32_t>(worldBatchStartVertex), 0);
    if (state.stats) {
        ++state.stats->worldFlushCount;
    }
    if (state.debugWorldFlushCount) {
        ++*state.debugWorldFlushCount;
    }

    worldBatchStartVertex = static_cast<uint32_t>(worldVertexPtr - worldVertexPtrHead);
    worldVertexCount = 0;
    worldIndexCount  = 0;
}

void FQuadRender::resetTextureBatch()
{
    _textureBindings.clear();
    _texturePtr2Idx.clear();
    _textureBindings.push_back(TextureBinding{
        .texture = TextureLibrary::get().getWhiteTexture(),
        .sampler = TextureLibrary::get().getDefaultSampler(),
    });
    _lastPushTextureSlot              = static_cast<int>(_textureBindings.size() - 1);
    _resourceVersion                  = std::max<uint64_t>(_resourceVersion + 1, 1);
    _uploadedScreenResourceVersion    = 0;
    _uploadedWorldResourceVersion     = 0;
}

void FQuadRender::updateFrameUBO(std::shared_ptr<IBuffer>& uboBuffer,
                                 const glm::mat4&          viewProj,
                                 const glm::mat4&          view)
{
    FrameUBO ubo{
        .viewProj = viewProj,
        .view     = view,
    };
    // The descriptor already points at this buffer (see ensureSlotResources).
    // Only the host-visible contents change per session.
    uboBuffer->writeData(&ubo, sizeof(ubo), 0);
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

FQuadRender::TextureRef FQuadRender::findOrAddTexture(ya::Ptr<Texture> texture, ETextureSampleMode mode)
{
    uint32_t textureIdx = 0;
    if (texture) {
        auto it = _texturePtr2Idx.find(texture.get());
        if (it != _texturePtr2Idx.end()) {
            textureIdx = it->second;
            // Atlas repacks replace the texture object even when the label is
            // unchanged; a pointer-keyed cache keeps each live atlas on its
            // own slot and only refreshes the binding when the object changes.
            if (_textureBindings[textureIdx].texture != texture) {
                _textureBindings[textureIdx].texture = texture;
                ++_resourceVersion;
            }
        }
        else {
            // The record step guarantees room (textureTableFull() checked
            // before registering list textures); an overflow here is a record
            // step contract violation, not a flush point.
            YA_CORE_ASSERT(_textureBindings.size() < TEXTURE_SET_SIZE,
                           "Render2D texture table overflow without a record-step check");
            _textureBindings.push_back(TextureBinding{
                .texture = texture,
                .sampler = resolveSamplerForTexture(texture.get()),
            });
            auto idx                        = static_cast<uint32_t>(_textureBindings.size() - 1);
            _texturePtr2Idx[texture.get()]   = idx;
            textureIdx                       = idx;
            _lastPushTextureSlot             = static_cast<int>(idx);
            ++_resourceVersion;
        }
    }
    return TextureRef{.slot = textureIdx, .mode = mode};
}

void FQuadRender::EmitScreenQuad(Vertex*                         out,
                                 const glm::mat4&                transform,
                                 TextureRef                      textureRef,
                                 const std::array<glm::vec4, 4>& colorsYaOrder,
                                 const glm::vec2&                uvScale,
                                 const glm::vec2&                uvTranslation,
                                 const glm::vec3&                corner)
{
    for (int i = 0; i < 4; i++) {
        out[i] = FQuadRender::Vertex{
            .pos            = transform * FQuadRender::vertices[i],
            .color          = colorsYaOrder[static_cast<size_t>(i)],
            .texCoord       = FQuadRender::defaultTexcoord[i] * uvScale + uvTranslation,
            .textureRef     = textureRef.encode(),
            .worldCenter    = glm::vec3(0.0f),
            .worldDirection = glm::vec3(0.0f, 0.0f, -1.0f),
            .worldSize      = glm::vec2(0.0f),
            .corner         = corner,
        };
    }
}

void FQuadRender::EmitWorldQuad(Vertex*          out,
                                const glm::vec3& center,
                                const glm::vec3& direction,
                                const glm::vec2& size,
                                TextureRef       textureRef,
                                const glm::vec4& tint,
                                const glm::vec2& uvScale)
{
    const glm::vec3 normalizedDirection = glm::length2(direction) > std::numeric_limits<float>::epsilon()
                                            ? glm::normalize(direction)
                                            : glm::vec3(0.0f, -1.0f, 0.0f);

    for (int i = 0; i < 4; i++) {
        out[i] = FQuadRender::Vertex{
            .pos         = glm::vec3(FQuadRender::vertices[i]),
            .color       = tint,
            .texCoord    = FQuadRender::defaultTexcoord[i] * uvScale,
            .textureRef  = textureRef.encode(),
            .worldCenter = center,
            .worldDirection = normalizedDirection,
            .worldSize   = size,
        };
    }
}

} // namespace ya
