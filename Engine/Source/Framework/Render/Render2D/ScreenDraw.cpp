#include "Render2D/ScreenDraw.h"

#include "Core/Common/DeferredDeletionQueue.h"
#include "Core/Log.h"
#include "Core/Math/GLM.h"
#include "RHI/Backend/TextureLibrary.h"
#include "RHI/Core/CommandBuffer.h"
#include "RHI/Core/RenderResourceFactory.h"
#include "RHI/Render.h"

#include <algorithm>
#include <cstring>
#include <format>
#include <limits>

namespace ya
{

namespace
{

ScreenDrawDiagnostics gScreenDrawDiagnostics{};

bool shouldLogFlush(uint32_t& counter)
{
    if (!gScreenDrawDiagnostics.bLogFlushBatches) {
        return false;
    }
    if (counter >= gScreenDrawDiagnostics.maxFlushLogsPerFrame) {
        return false;
    }
    ++counter;
    return true;
}

void setScreenViewportAndScissor(ICommandBuffer& cmdBuf, IRender* render, uint32_t width, uint32_t height)
{
    float viewportY      = 0.0f;
    float viewportHeight = static_cast<float>(height);
    if (render && render->getAPI() == ERenderAPI::Vulkan) {
        viewportY      = static_cast<float>(height);
        viewportHeight = -static_cast<float>(height);
    }
    cmdBuf.setViewport(0.0f, viewportY, static_cast<float>(width), viewportHeight, 0.0f, 1.0f);
    cmdBuf.setScissor(0, 0, width, height);
}

ViewportState buildScreenViewportState()
{
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

ya::Ptr<Sampler> resolveSamplerForTexture(Texture* texture)
{
    if (!texture) {
        return TextureLibrary::get().getDefaultSampler();
    }
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

std::vector<VertexAttribute> buildScreenVertexAttributes()
{
    return std::vector<VertexAttribute>{
        VertexAttribute{.bufferSlot = 0, .location = 0, .format = EVertexAttributeFormat::Float3, .offset = offsetof(ScreenVertex, pos)},
        VertexAttribute{.bufferSlot = 0, .location = 1, .format = EVertexAttributeFormat::Float4, .offset = offsetof(ScreenVertex, color)},
        VertexAttribute{.bufferSlot = 0, .location = 2, .format = EVertexAttributeFormat::Float2, .offset = offsetof(ScreenVertex, texCoord)},
        VertexAttribute{.bufferSlot = 0, .location = 3, .format = EVertexAttributeFormat::Uint, .offset = offsetof(ScreenVertex, textureSlot)},
        VertexAttribute{.bufferSlot = 0, .location = 4, .format = EVertexAttributeFormat::Uint, .offset = offsetof(ScreenVertex, sampleMode)},
        VertexAttribute{.bufferSlot = 0, .location = 5, .format = EVertexAttributeFormat::Float3, .offset = offsetof(ScreenVertex, corner)},
    };
}

GraphicsPipelineCreateInfo buildScreenPipelineCI(IPipelineLayout*   pipelineLayout,
                                                 const std::string& label,
                                                 EFormat::T         colorFormat,
                                                 EFormat::T         depthFormat)
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
            .sourceMode = ShaderDesc::ESourceMode::StageFiles,
            .stageFiles = {
                ShaderDesc::StageFile{.stage = EShaderStage::Vertex, .file = "Sprite2DScreen.slang", .entryName = "vertMain"},
                ShaderDesc::StageFile{.stage = EShaderStage::Fragment, .file = "Sprite2DScreen.slang", .entryName = "fragMain"},
            },
            .vertexBufferDescs = {VertexBufferDescription{.slot = 0, .pitch = sizeof(ScreenVertex)}},
            .vertexAttributes  = buildScreenVertexAttributes(),
            .defines = {
                std::format("TEXTURE_SET_SIZE {}", kScreenTextureSetSize),
                std::format("SAMPLE_MODE_COVERAGE {}", static_cast<uint32_t>(EScreenTextureSampleMode::Coverage)),
                std::format("SAMPLE_MODE_SDF {}", static_cast<uint32_t>(EScreenTextureSampleMode::Sdf)),
                std::format("SAMPLE_MODE_OPAQUE {}", static_cast<uint32_t>(EScreenTextureSampleMode::Opaque)),
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
            .attachments    = {ColorBlendAttachmentState{
                .index               = 0,
                .bBlendEnable        = true,
                .srcColorBlendFactor = EBlendFactor::SrcAlpha,
                .dstColorBlendFactor = EBlendFactor::OneMinusSrcAlpha,
                .colorBlendOp        = EBlendOp::Add,
                .srcAlphaBlendFactor = EBlendFactor::One,
                .dstAlphaBlendFactor = EBlendFactor::Zero,
                .alphaBlendOp        = EBlendOp::Add,
                .colorWriteMask      = static_cast<EColorComponent::T>(EColorComponent::R | EColorComponent::G | EColorComponent::B | EColorComponent::A),
            }},
        },
        .viewportState = buildScreenViewportState(),
    };
}

PipelineLayoutDesc screenPipelineLayoutDesc()
{
    return PipelineLayoutDesc{
        .pushConstants        = {},
        .descriptorSetLayouts = {
            DescriptorSetLayoutDesc{
                .label    = "Frame_UBO",
                .set      = 0,
                .bindings = {DescriptorSetLayoutBinding{
                    .binding         = 0,
                    .descriptorType  = EPipelineDescriptorType::UniformBuffer,
                    .descriptorCount = 1,
                    .stageFlags      = EShaderStage::Vertex | EShaderStage::Fragment,
                }},
            },
            DescriptorSetLayoutDesc{
                .label    = "CombinedImageSampler",
                .set      = 0,
                .bindings = {DescriptorSetLayoutBinding{
                    .binding         = 0,
                    .descriptorType  = EPipelineDescriptorType::CombinedImageSampler,
                    .descriptorCount = kScreenTextureSetSize,
                    .stageFlags      = EShaderStage::Fragment,
                }},
            },
        },
    };
}

} // namespace

ScreenDrawDiagnostics& screenDrawDiagnostics()
{
    return gScreenDrawDiagnostics;
}

void ScreenDrawPipelines::init(IRender* render)
{
    _render = render;
    const PipelineLayoutDesc layoutDesc = screenPipelineLayoutDesc();
    _frameUboDSL = IDescriptorSetLayout::create(render, layoutDesc.descriptorSetLayouts[0]);
    _resourceDSL = IDescriptorSetLayout::create(render, layoutDesc.descriptorSetLayouts[1]);
    _pipelineLayout = IPipelineLayout::create(render, "Sprite2D_PipelineLayout", layoutDesc.pushConstants, {_frameUboDSL, _resourceDSL});
}

void ScreenDrawPipelines::destroy()
{
    for (auto& variant : _variants) {
        DeferredDeletionQueue::get().retire(std::move(variant.pipeline));
    }
    _variants.clear();
    auto& queue = DeferredDeletionQueue::get();
    queue.retire(std::move(_pipelineLayout));
    queue.retire(std::move(_frameUboDSL));
    queue.retire(std::move(_resourceDSL));
    _render = nullptr;
}

IGraphicsPipeline* ScreenDrawPipelines::prepare(EFormat::T colorFormat, EFormat::T depthFormat)
{
    if (!_render || colorFormat == EFormat::Undefined) {
        return nullptr;
    }
    for (auto& variant : _variants) {
        if (variant.color == colorFormat && variant.depth == depthFormat && variant.pipeline) {
            return variant.pipeline.get();
        }
    }
    auto pipeline = IGraphicsPipeline::create(_render);
    pipeline->recreate(buildScreenPipelineCI(
        _pipelineLayout.get(),
        depthFormat == EFormat::Undefined ? "Sprite2D_UI_Pipeline" : "Sprite2D_Screen_Pipeline",
        colorFormat,
        depthFormat));
    _variants.push_back(Variant{
        .color    = colorFormat,
        .depth    = depthFormat,
        .pipeline = std::move(pipeline),
    });
    return _variants.back().pipeline.get();
}

void ScreenDrawRecorder::init(ScreenDrawPipelines& pipelines)
{
    _pipelines = &pipelines;
    _render    = pipelines.render();
    YA_CORE_ASSERT(_render != nullptr, "ScreenDrawRecorder requires an initialized pipeline cache");

    constexpr uint32_t frameCount = MAX_FLIGHTS_IN_FLIGHT;
    constexpr uint32_t imageSets  = frameCount * kResourceDescriptorSets;
    _descriptorPool = IDescriptorPool::create(
        _render,
        DescriptorPoolCreateInfo{
            .maxSets   = frameCount * 2 + imageSets * 2,
            .poolSizes = {
                DescriptorPoolSize{.type = EPipelineDescriptorType::UniformBuffer, .descriptorCount = frameCount * 2},
                DescriptorPoolSize{.type = EPipelineDescriptorType::CombinedImageSampler, .descriptorCount = imageSets * kScreenTextureSetSize * 2},
            },
        });
    ensureResources();
}

void ScreenDrawRecorder::destroy()
{
    auto& queue = DeferredDeletionQueue::get();
    for (auto& flight : _flights) {
        queue.retire(std::move(flight.vertexBuffer));
        queue.retire(std::move(flight.indexBuffer));
        queue.retire(std::move(flight.frameUBOBuffer));
        flight.vertexPtrHead = nullptr;
        flight.indexPtrHead = nullptr;
        flight.frameUboDS = {};
        flight.resourceDSPool.clear();
        flight.activeResourceDS = {};
        flight.nextResourceDS = 0;
    }
    queue.retire(std::move(_descriptorPool));
    _vertexPtr = nullptr;
    _vertexPtrHead = nullptr;
    _indexPtr = nullptr;
    _pipeline = nullptr;
    _pipelines = nullptr;
    _render = nullptr;
}

void ScreenDrawRecorder::prepare(EFormat::T colorFormat, EFormat::T depthFormat)
{
    YA_CORE_ASSERT(_pipelines != nullptr, "ScreenDrawRecorder::prepare before init");
    _colorFormat = colorFormat;
    _depthFormat = depthFormat;
    _pipeline = _pipelines->prepare(colorFormat, depthFormat);
}

void ScreenDrawRecorder::ensureResources()
{
    if (_flights[0].frameUBOBuffer) {
        return;
    }
    std::vector<DescriptorSetHandle> descriptorSets;
    _descriptorPool->allocateDescriptorSets(_pipelines->_frameUboDSL, MAX_FLIGHTS_IN_FLIGHT, descriptorSets);
    for (uint32_t flight = 0; flight < MAX_FLIGHTS_IN_FLIGHT; ++flight) {
        auto& resources = _flights[flight];
        resources.frameUboDS = descriptorSets[flight];
        resources.frameUBOBuffer = _render->getResourceFactory()->createBuffer(BufferCreateInfo{
            .label       = std::format("Sprite2D_{}_FrameUBO", flight),
            .usage       = EBufferUsage::UniformBuffer,
            .size        = sizeof(FrameUBO),
            .memoryUsage = EMemoryUsage::CpuToGpu,
        });
        _render->getDescriptorHelper()->updateDescriptorSets({
            IDescriptorSetHelper::writeOneUniformBuffer(resources.frameUboDS, 0, resources.frameUBOBuffer.get()),
        });
    }

    descriptorSets.clear();
    _descriptorPool->allocateDescriptorSets(
        _pipelines->_resourceDSL,
        MAX_FLIGHTS_IN_FLIGHT * kResourceDescriptorSets,
        descriptorSets);
    for (uint32_t flight = 0; flight < MAX_FLIGHTS_IN_FLIGHT; ++flight) {
        auto& resources = _flights[flight];
        resources.resourceDSPool.reserve(kResourceDescriptorSets);
        const size_t base = static_cast<size_t>(flight) * kResourceDescriptorSets;
        for (uint32_t i = 0; i < kResourceDescriptorSets; ++i) {
            resources.resourceDSPool.push_back(descriptorSets[base + i]);
        }
    }

    for (uint32_t flight = 0; flight < MAX_FLIGHTS_IN_FLIGHT; ++flight) {
        auto& resources = _flights[flight];
        resources.vertexBuffer = _render->getResourceFactory()->createBuffer(BufferCreateInfo{
            .label       = std::format("Sprite2D_{}_Screen_VertexBuffer", flight),
            .usage       = EBufferUsage::VertexBuffer | EBufferUsage::TransferDst,
            .size        = sizeof(ScreenVertex) * ScreenDrawPipelines::MaxVertexCount * ScreenDrawPipelines::kFrameFlushSlots,
            .memoryUsage = EMemoryUsage::CpuToGpu,
        });
        resources.vertexPtrHead = resources.vertexBuffer->map<ScreenVertex>();
        resources.indexBuffer = _render->getResourceFactory()->createBuffer(BufferCreateInfo{
            .label       = std::format("Sprite2D_{}_Screen_IndexBuffer", flight),
            .usage       = EBufferUsage::IndexBuffer | EBufferUsage::TransferDst,
            .size        = sizeof(uint32_t) * ScreenDrawPipelines::MaxIndexCount * ScreenDrawPipelines::kFrameFlushSlots,
            .memoryUsage = EMemoryUsage::CpuToGpu,
        });
        resources.indexPtrHead = resources.indexBuffer->map<uint32_t>();
    }
}

void ScreenDrawRecorder::begin(const Extent2D& extent, uint32_t flightSlot)
{
    _activeFlightIndex = flightSlot % MAX_FLIGHTS_IN_FLIGHT;
    auto& resources = activeFlight();
    _vertexPtrHead = resources.vertexPtrHead;
    _vertexPtr = _vertexPtrHead;
    _indexPtr = resources.indexPtrHead;
    _vertexCount = 0;
    _indexCount = 0;
    _batchStartVertex = 0;
    _batchStartIndex = 0;
    _resourceVersion = 1;
    _uploadedResourceVersion = 0;
    _frameUboUploaded = false;
    resources.activeResourceDS = {};
    resources.nextResourceDS = 0;
    _textureBindings.clear();

    const float w = static_cast<float>(extent.width);
    const float h = static_cast<float>(extent.height);
    _screenOrthoProj = glm::orthoRH_ZO(0.0f, w, h, 0.0f, -1.0f, 1.0f);
}

void ScreenDrawRecorder::flush(ICommandBuffer* cmdBuf, uint32_t width, uint32_t height, bool bClipped, const Rect2D& clip, ScreenDrawFrameStats* stats)
{
    if (!cmdBuf || _vertexCount == 0 || _indexCount == 0) {
        return;
    }
    auto& resources = activeFlight();
    resources.vertexBuffer->flush();
    resources.indexBuffer->flush();
    YA_CORE_ASSERT(_pipeline != nullptr, "Screen draw pipeline was not prepared before command recording");
    cmdBuf->bindPipeline(_pipeline);
    setScreenViewportAndScissor(*cmdBuf, _render, width, height);
    if (bClipped) {
        const int32_t sx = std::clamp(static_cast<int32_t>(clip.pos.x), 0, static_cast<int32_t>(width));
        const int32_t sy = std::clamp(static_cast<int32_t>(clip.pos.y), 0, static_cast<int32_t>(height));
        const int32_t sw = std::clamp(static_cast<int32_t>(clip.extent.x), 0, static_cast<int32_t>(width) - sx);
        const int32_t sh = std::clamp(static_cast<int32_t>(clip.extent.y), 0, static_cast<int32_t>(height) - sy);
        cmdBuf->setScissor(sx, sy, static_cast<uint32_t>(sw), static_cast<uint32_t>(sh));
    }
    else {
        cmdBuf->setScissor(0, 0, width, height);
    }
    if (_render && _render->getCapabilities().dynamicCullMode) {
        cmdBuf->setCullMode(gScreenDrawDiagnostics.screenCullMode);
    }

    if (_uploadedResourceVersion != _resourceVersion) {
        resources.activeResourceDS = acquireResourceDS(resources);
        updateResources(resources.activeResourceDS);
        _uploadedResourceVersion = _resourceVersion;
    }
    if (!_frameUboUploaded) {
        FrameUBO ubo{.viewProj = _screenOrthoProj, .view = glm::mat4(1.0f)};
        resources.frameUBOBuffer->writeData(&ubo, sizeof(ubo), 0);
        _frameUboUploaded = true;
    }

    const uint32_t cursorVertex = static_cast<uint32_t>(_vertexPtr - _vertexPtrHead);
    YA_CORE_ASSERT(cursorVertex == _batchStartVertex + _vertexCount, "Screen draw batch cursor mismatch");
    YA_CORE_ASSERT(static_cast<uint64_t>(_batchStartVertex) + _vertexCount <=
                       ScreenDrawPipelines::MaxVertexCount * ScreenDrawPipelines::kFrameFlushSlots,
                   "Screen draw frame exceeded vertex buffer capacity");
    YA_CORE_ASSERT(static_cast<uint64_t>(_batchStartIndex) + _indexCount <=
                       ScreenDrawPipelines::MaxIndexCount * ScreenDrawPipelines::kFrameFlushSlots,
                   "Screen draw frame exceeded index buffer capacity");
    if (gScreenDrawDiagnostics.bLogFlushBatches) {
        uint32_t counter = stats ? stats->screenFlushCount : 0u;
        if (shouldLogFlush(counter)) {
            YA_CORE_INFO("Screen draw flush: flight={} startVertex={} vertexCount={} textures={}",
                         _activeFlightIndex, _batchStartVertex, _vertexCount, _textureBindings.size());
        }
    }

    cmdBuf->bindDescriptorSets(_pipelines->layout(), 0, {resources.frameUboDS, resources.activeResourceDS});
    cmdBuf->bindVertexBuffer(0, resources.vertexBuffer.get(), 0);
    cmdBuf->bindIndexBuffer(resources.indexBuffer.get(), 0, false);
    cmdBuf->drawIndexed(static_cast<uint32_t>(_indexCount), 1, _batchStartIndex, static_cast<int32_t>(_batchStartVertex), 0);

    if (stats) {
        ++stats->screenFlushCount;
        stats->screenVertexCount += _vertexCount;
        stats->screenIndexCount += _indexCount;
    }
    _batchStartVertex = static_cast<uint32_t>(_vertexPtr - _vertexPtrHead);
    _batchStartIndex += _indexCount;
    _vertexCount = 0;
    _indexCount = 0;
}

void ScreenDrawRecorder::updateResources(DescriptorSetHandle dsHandle)
{
    std::vector<DescriptorImageInfo> imageInfos;
    auto defaultSampler = TextureLibrary::get().getDefaultSampler();
    auto whiteTexture = TextureLibrary::get().getWhiteTexture();
    imageInfos.reserve(kScreenTextureSetSize);
    for (uint32_t i = 0; i < kScreenTextureSetSize; ++i) {
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
        {IDescriptorSetHelper::genImageWrite(dsHandle, 0, 0, EPipelineDescriptorType::CombinedImageSampler, imageInfos)},
        {});
}

DescriptorSetHandle ScreenDrawRecorder::acquireResourceDS(Flight& flight)
{
    YA_CORE_ASSERT(flight.nextResourceDS < flight.resourceDSPool.size(),
                   "Screen draw exhausted resource descriptor sets");
    return flight.resourceDSPool[flight.nextResourceDS++];
}

ScreenDrawFrameStats ScreenDrawRecorder::record(ScreenDrawList& list, const ScreenDrawTarget& target)
{
    ScreenDrawFrameStats stats{};
    if (!target.cmd || !_pipelines) {
        return stats;
    }
    list.seal();
    YA_CORE_ASSERT(_pipeline != nullptr && _colorFormat == target.colorFormat,
                   "ScreenDrawRecorder::record requires prepare() for this color format");

    const uint32_t framesInFlight = std::max(1u, _render->framesInFlight());
    const uint32_t flightSlot = static_cast<uint32_t>(_render->recordedFrameIndex() % framesInFlight);
    begin(Extent2D{.width = target.width, .height = target.height}, flightSlot);

    std::vector<TextureTableKey> catalogKeys;
    catalogKeys.reserve(list.textures.size());
    for (const Ptr<Texture>& texture : list.textures) {
        catalogKeys.push_back(TextureTableKey::fromTexture(texture.get()));
    }
    std::vector<ScreenDrawCommandSpan> spans;
    spans.reserve(list.commands.size());
    for (const ScreenDrawList::Command& command : list.commands) {
        spans.push_back(ScreenDrawCommandSpan{
            .firstIndex = command.firstIndex,
            .indexCount = command.indexCount,
            .bClipped   = command.bClipped,
            .clip       = command.clip,
        });
    }
    const ScreenDrawRemap remap = planScreenDrawRemap(
        list.indices,
        static_cast<uint32_t>(list.vertices.size()),
        [&](uint32_t vertex) { return list.vertices[vertex].textureSlot; },
        catalogKeys,
        TextureTableKey::fromTexture(nullptr),
        spans,
        static_cast<uint32_t>(ScreenDrawPipelines::MaxVertexCount),
        static_cast<uint32_t>(ScreenDrawPipelines::MaxIndexCount),
        kScreenTextureSetSize);

    std::vector<uint32_t> uploadedSlots;
    for (const ScreenDrawBatchRange& batch : remap.batches) {
        YA_CORE_ASSERT(_vertexPtr != nullptr && _indexPtr != nullptr, "Screen draw buffers are not mapped");
        for (uint32_t i = 0; i < batch.vertexCount; ++i) {
            const uint32_t source = remap.sourceVertices[batch.firstVertex + i];
            ScreenVertex vertex   = list.vertices[source];
            vertex.textureSlot    = remap.gpuSlots[batch.firstVertex + i];
            *_vertexPtr++         = vertex;
        }
        uint32_t* dst = _indexPtr + _batchStartIndex;
        for (uint32_t i = 0; i < batch.indexCount; ++i) {
            dst[i] = remap.indices[batch.firstIndex + i];
        }
        _vertexCount = batch.vertexCount;
        _indexCount  = batch.indexCount;

        if (uploadedSlots != batch.catalogSlots) {
            _textureBindings.clear();
            _textureBindings.reserve(batch.catalogSlots.size());
            for (uint32_t catalog : batch.catalogSlots) {
                if (catalog == kScreenDrawWhiteCatalog) {
                    _textureBindings.push_back(TextureBinding{
                        .texture = TextureLibrary::get().getWhiteTexture(),
                        .sampler = TextureLibrary::get().getDefaultSampler(),
                    });
                }
                else {
                    const Ptr<Texture> texture = list.textures[catalog];
                    _textureBindings.push_back(TextureBinding{
                        .texture = texture,
                        .sampler = resolveSamplerForTexture(texture.get()),
                    });
                }
            }
            _resourceVersion         = std::max<uint64_t>(_resourceVersion + 1, 1);
            _uploadedResourceVersion = 0;
            uploadedSlots            = batch.catalogSlots;
        }
        flush(target.cmd, target.width, target.height, batch.bClipped, batch.clip, &stats);
    }
    return stats;
}

} // namespace ya
