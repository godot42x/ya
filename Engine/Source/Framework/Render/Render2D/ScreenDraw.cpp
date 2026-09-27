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
    resetTextureBatch();

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

void ScreenDrawRecorder::resetTextureBatch()
{
    _textureBindings.clear();
    _texturePtr2Idx.clear();
    _textureBindings.push_back(TextureBinding{
        .texture = TextureLibrary::get().getWhiteTexture(),
        .sampler = TextureLibrary::get().getDefaultSampler(),
    });
    _lastPushTextureSlot = static_cast<int>(_textureBindings.size() - 1);
    _resourceVersion = std::max<uint64_t>(_resourceVersion + 1, 1);
    _uploadedResourceVersion = 0;
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

uint32_t ScreenDrawRecorder::findOrAddTexture(ya::Ptr<Texture> texture)
{
    uint32_t textureIdx = 0;
    if (texture) {
        auto it = _texturePtr2Idx.find(texture.get());
        if (it != _texturePtr2Idx.end()) {
            textureIdx = it->second;
            if (_textureBindings[textureIdx].texture != texture) {
                _textureBindings[textureIdx].texture = texture;
                ++_resourceVersion;
            }
        }
        else {
            YA_CORE_ASSERT(_textureBindings.size() < kScreenTextureSetSize,
                           "Screen draw texture table overflow without a record-step check");
            _textureBindings.push_back(TextureBinding{
                .texture = texture,
                .sampler = resolveSamplerForTexture(texture.get()),
            });
            textureIdx = static_cast<uint32_t>(_textureBindings.size() - 1);
            _texturePtr2Idx[texture.get()] = textureIdx;
            _lastPushTextureSlot = static_cast<int>(textureIdx);
            ++_resourceVersion;
        }
    }
    return textureIdx;
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

    constexpr uint32_t kUnmapped = ~0u;
    std::vector<uint32_t> localToGlobal(list.textures.size(), kUnmapped);
    std::unordered_map<uint32_t, uint32_t> copied;
    bool bOpen = false;
    bool bClipped = false;
    Rect2D clip{};

    auto flushBatch = [&]() {
        flush(target.cmd, target.width, target.height, bClipped, clip, &stats);
        copied.clear();
    };
    auto flushOpen = [&]() {
        if (!bOpen) {
            return;
        }
        flushBatch();
        bOpen = false;
    };

    auto placeTriangle = [&](uint32_t i0, uint32_t i1, uint32_t i2) {
        const uint32_t src[3] = {i0, i1, i2};
        for (int attempt = 0; attempt < 8; ++attempt) {
            uint32_t uniqueNew[3]{};
            uint32_t newVerts = 0;
            bool bNeedTextureFlush = false;
            for (uint32_t s : src) {
                if (copied.contains(s)) {
                    continue;
                }
                bool bSeen = false;
                for (uint32_t n = 0; n < newVerts; ++n) {
                    if (uniqueNew[n] == s) {
                        bSeen = true;
                        break;
                    }
                }
                if (bSeen) {
                    continue;
                }
                uniqueNew[newVerts++] = s;
                const uint32_t slot = list.vertices[s].textureSlot;
                YA_CORE_ASSERT(slot < localToGlobal.size(), "Screen draw texture slot out of range");
                if (localToGlobal[slot] == kUnmapped && textureTableFull()) {
                    bNeedTextureFlush = true;
                }
            }
            if (bNeedTextureFlush) {
                if (_vertexCount > 0) {
                    flushBatch();
                }
                resetTextureBatch();
                std::fill(localToGlobal.begin(), localToGlobal.end(), kUnmapped);
                bOpen = true;
                continue;
            }
            const bool bFits = _vertexCount + newVerts <= ScreenDrawPipelines::MaxVertexCount
                && _indexCount + 3 <= ScreenDrawPipelines::MaxIndexCount;
            if (!bFits) {
                YA_CORE_ASSERT(_vertexCount > 0 || _indexCount > 0,
                               "Screen draw triangle does not fit in an empty batch");
                flushBatch();
                continue;
            }
            auto put = [&](uint32_t s) -> uint32_t {
                if (const auto it = copied.find(s); it != copied.end()) {
                    return it->second;
                }
                const uint32_t slot = list.vertices[s].textureSlot;
                if (localToGlobal[slot] == kUnmapped) {
                    localToGlobal[slot] = findOrAddTexture(list.textures[slot].get());
                }
                ScreenVertex vertex = list.vertices[s];
                vertex.textureSlot = localToGlobal[slot];
                *_vertexPtr++ = vertex;
                const uint32_t local = _vertexCount++;
                copied.emplace(s, local);
                return local;
            };
            YA_CORE_ASSERT(_indexPtr != nullptr, "Screen draw index buffer is not mapped");
            uint32_t* dst = _indexPtr + _batchStartIndex + _indexCount;
            dst[0] = put(i0);
            dst[1] = put(i1);
            dst[2] = put(i2);
            _indexCount += 3;
            return;
        }
        YA_CORE_ASSERT(false, "Screen draw failed to place a triangle");
    };

    for (const ScreenDrawList::Command& command : list.commands) {
        const bool bBoundary = !bOpen || bClipped != command.bClipped
            || (command.bClipped && (clip.pos != command.clip.pos || clip.extent != command.clip.extent));
        if (bBoundary) {
            flushOpen();
            bOpen = true;
            bClipped = command.bClipped;
            clip = command.clip;
        }
        YA_CORE_ASSERT(command.indexCount % 3 == 0, "Screen draw command index count is not a triangle list");
        const uint32_t indexEnd = command.firstIndex + command.indexCount;
        for (uint32_t index = command.firstIndex; index < indexEnd; index += 3) {
            placeTriangle(list.indices[index], list.indices[index + 1], list.indices[index + 2]);
        }
    }
    flushOpen();
    return stats;
}

} // namespace ya
