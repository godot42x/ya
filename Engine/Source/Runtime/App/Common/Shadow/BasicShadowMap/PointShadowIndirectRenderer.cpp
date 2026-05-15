#include "PointShadowIndirectRenderer.h"

#include "Render/Core/CommandBuffer.h"
#include "Render/Mesh.h"
#include "Render/Render.h"
#include "Render/RenderFrameData.h"

#include <algorithm>
#include <format>
#include <utility>

namespace ya
{

void PointShadowIndirectRenderer::init(IRender* render, stdptr<IDescriptorSetLayout> frameDSL)
{
    _render   = render;
    _frameDSL = std::move(frameDSL);

    const auto& caps = _render->getCapabilities();
    _bSupported = caps.computeShader && caps.drawIndexedIndirect && caps.storageBuffer && caps.drawIndexedIndirectCount;
    if (!_bSupported) return;

    _indirectDSL = IDescriptorSetLayout::create(_render, DescriptorSetLayoutDesc{
        .label    = "PointShadow_Indirect_DSL",
        .set      = 1,
        .bindings = {{.binding = 0, .descriptorType = EPipelineDescriptorType::StorageBuffer, .descriptorCount = 1, .stageFlags = EShaderStage::Vertex}},
    });

    _pipelineCI = GraphicsPipelineCreateInfo{
        .pipelineRenderingInfo = {.label = "Point Shadow Indirect", .depthAttachmentFormat = EFormat::D32_SFLOAT},
        .shaderDesc            = ShaderDesc{.shaderName = "Shadow/PointShadowIndirect.slang"},
        .dynamicFeatures       = {EPipelineDynamicFeature::Viewport, EPipelineDynamicFeature::Scissor},
        .primitiveType         = EPrimitiveType::TriangleList,
        .rasterizationState    = {.polygonMode = EPolygonMode::Fill, .cullMode = ECullMode::Front, .frontFace = EFrontFaceType::CounterClockWise},
        .depthStencilState     = {.bDepthTestEnable = true, .bDepthWriteEnable = true, .depthCompareOp = ECompareOp::LessOrEqual},
        .colorBlendState       = {.attachments = {}},
        .viewportState         = {.viewports = {Viewport::defaults()}, .scissors = {Scissor::defaults()}},
    };

    _staticVariant.pipelineLayout = IPipelineLayout::create(
        _render, "PointShadow_Indirect_Static_PPL", {}, {_frameDSL, _indirectDSL});

    auto indirectCI = _pipelineCI;
    indirectCI.pipelineLayout = _staticVariant.pipelineLayout.get();
    indirectCI.shaderDesc.vertexBufferDescs = {VertexBufferDescription{.slot = 0, .pitch = sizeof(Vertex)}};
    indirectCI.shaderDesc.vertexAttributes  = {{.bufferSlot = 0, .location = 0, .format = EVertexAttributeFormat::Float3, .offset = offsetof(Vertex, position)}};
    _staticVariant.pipeline = IGraphicsPipeline::create(_render);
    YA_CORE_ASSERT(_staticVariant.pipeline && _staticVariant.pipeline->recreate(indirectCI),
                   "Failed to create point shadow indirect static pipeline");

    _dsp = IDescriptorPool::create(_render, DescriptorPoolCreateInfo{
        .label     = "PointShadowIndirect_DSP",
        .maxSets   = MAX_FLIGHTS_IN_FLIGHT,
        .poolSizes = {{.type = EPipelineDescriptorType::StorageBuffer, .descriptorCount = MAX_FLIGHTS_IN_FLIGHT}},
    });

    _cullPass.init(_render);
}

void PointShadowIndirectRenderer::destroy()
{
    _cullPass.destroy();

    for (auto& flight : _perFlight) {
        flight.instanceBuffer.reset();
        flight.noCullDrawCommandBuffer.reset();
        flight.noCullDrawCountBuffer.reset();
        flight.indirectDS = nullptr;
        flight.activeCullPlugin = ECullPlugin::Disabled;
        flight.meshBatches.clear();
        flight.totalInstances = 0;
    }

    _dsp.reset();
    _staticVariant = {};
    _indirectDSL.reset();
    _frameDSL.reset();
    _instanceCapacity   = 0;
    _noCullFaceCapacity = 0;
    _meshBatchMap.clear();
    _bSupported = false;
    _render = nullptr;
}

void PointShadowIndirectRenderer::beginFrame()
{
    if (_staticVariant.pipeline) _staticVariant.pipeline->beginFrame();
}

void PointShadowIndirectRenderer::prepare(const BasicShadowFramePayload& payload)
{
    auto& flight = _perFlight[payload.flightIndex];
    flight.activeCullPlugin = ECullPlugin::Disabled;
    flight.totalInstances = 0;

    if (!_bSupported || !payload.pointIndirectRequested || !payload.frameData || payload.pointLightCount == 0) return;

    flight.activeCullPlugin = payload.pointIndirectCullEnabled ? ECullPlugin::Compute : ECullPlugin::NoCull;

    std::vector<PointShadowInstanceData> instanceData;
    flight.meshBatches.clear();
    _meshBatchMap.clear();

    auto addItems = [&](const std::vector<RenderDrawItem>& items)
    {
        for (const auto& item : items) {
            if (!item.mesh) continue;

            auto [it, inserted] = _meshBatchMap.try_emplace(item.mesh, static_cast<uint32_t>(flight.meshBatches.size()));
            if (inserted) {
                flight.meshBatches.push_back(MeshBatch{
                    .mesh          = item.mesh,
                    .firstInstance = static_cast<uint32_t>(instanceData.size()),
                    .instanceCount = 0,
                });
            }
            auto& batch = flight.meshBatches[it->second];
            batch.instanceCount++;

            const auto      worldBounds = item.mesh->boundingBox.transformed(item.worldMatrix);
            const glm::vec3 center      = 0.5f * (worldBounds.min + worldBounds.max);
            const float     radius      = glm::length(worldBounds.max - center);

            instanceData.push_back(PointShadowInstanceData{
                .worldMatrix    = item.worldMatrix,
                .boundingSphere = glm::vec4(center, radius),
                .indexCount     = item.mesh->getIndexCount(),
                .firstIndex     = 0,
                .vertexOffset   = 0,
            });
        }
    };

    const auto& staticBuckets = payload.frameData->drawBuckets.staticMeshes;
    addItems(staticBuckets.pbrDrawItems);
    addItems(staticBuckets.phongDrawItems);
    addItems(staticBuckets.unlitDrawItems);
    addItems(staticBuckets.simpleDrawItems);
    addItems(staticBuckets.fallbackDrawItems);

    flight.totalInstances = static_cast<uint32_t>(instanceData.size());
    if (instanceData.empty()) {
        flight.activeCullPlugin = ECullPlugin::Disabled;
        return;
    }

    ensureInstanceCapacity(flight.totalInstances);
    flight.instanceBuffer->writeData(instanceData.data(), instanceData.size() * sizeof(PointShadowInstanceData), 0);
    flight.instanceBuffer->flush();

    const uint32_t activeFaces = payload.pointLightCount * 6;
    if (flight.activeCullPlugin == ECullPlugin::NoCull) {
        uploadNoCullCommands(payload.flightIndex, activeFaces, instanceData);
        return;
    }

    std::vector<PointShadowFaceFrustum> frustums(activeFaces);
    for (uint32_t light = 0; light < payload.pointLightCount; ++light) {
        for (uint32_t face = 0; face < 6; ++face) {
            const glm::mat4 viewProj = payload.frameUBO.pointLights[light].matrix[face];
            auto planes = extractFrustumPlanes(viewProj);
            auto& frustum = frustums[light * 6 + face];
            for (uint32_t p = 0; p < 6; ++p) {
                frustum.planes[p] = planes[p];
            }
        }
    }

    _cullPass.bindInstanceBuffer(payload.flightIndex, flight.instanceBuffer.get());
    _cullPass.prepare(payload.flightIndex, frustums.data(), activeFaces, flight.totalInstances);
    updateIndirectDescriptors(payload.flightIndex);
}

void PointShadowIndirectRenderer::dispatchCull(ICommandBuffer* cmdBuf, uint32_t flightIndex) const
{
    const auto& flight = _perFlight[flightIndex];
    if (!_bSupported || flight.activeCullPlugin != ECullPlugin::Compute) return;
    _cullPass.dispatch(cmdBuf, flightIndex);
}

void PointShadowIndirectRenderer::renderFace(ICommandBuffer*                cmdBuf,
                                             const BasicShadowFramePayload& payload,
                                             const PointShadowFacePayload&  facePayload) const
{
    const auto& flight = _perFlight[payload.flightIndex];

    cmdBuf->bindPipeline(_staticVariant.pipeline.get());
    cmdBuf->bindDescriptorSets(_staticVariant.pipelineLayout.get(), 0,
                               {facePayload.faceDS, flight.indirectDS});

    auto* drawCmdBuffer = flight.activeCullPlugin == ECullPlugin::Compute
                            ? _cullPass.getDrawCommandBuffer(payload.flightIndex)
                            : flight.noCullDrawCommandBuffer.get();
    auto* drawCountBuf  = flight.activeCullPlugin == ECullPlugin::Compute
                            ? _cullPass.getDrawCountBuffer(payload.flightIndex)
                            : flight.noCullDrawCountBuffer.get();

    const uint64_t drawOffset  = static_cast<uint64_t>(facePayload.faceGlobalIndex) * ShadowConstants::MAX_DRAWS_PER_FACE * sizeof(PointShadowIndirectCommand);
    const uint64_t countOffset = static_cast<uint64_t>(facePayload.faceGlobalIndex) * sizeof(uint32_t);

    if (!flight.meshBatches.empty() && flight.meshBatches[0].mesh) {
        auto* mesh = flight.meshBatches[0].mesh;
        cmdBuf->bindVertexBuffer(0, mesh->getVertexBuffer(), mesh->getVertexBufferOffset());
        cmdBuf->bindIndexBuffer(mesh->getIndexBufferMut(), mesh->getIndexBufferOffset(), false);
    }

    cmdBuf->drawIndexedIndirectCount(
        drawCmdBuffer, drawOffset,
        drawCountBuf, countOffset,
        ShadowConstants::MAX_DRAWS_PER_FACE,
        sizeof(PointShadowIndirectCommand));
}

void PointShadowIndirectRenderer::refreshPipeline(EFormat::T depthFormat)
{
    _pipelineCI.pipelineRenderingInfo.depthAttachmentFormat = depthFormat;
    if (_staticVariant.pipeline) {
        auto ci = _pipelineCI;
        ci.pipelineLayout = _staticVariant.pipelineLayout.get();
        ci.shaderDesc.vertexBufferDescs = {VertexBufferDescription{.slot = 0, .pitch = sizeof(Vertex)}};
        ci.shaderDesc.vertexAttributes  = {{.bufferSlot = 0, .location = 0, .format = EVertexAttributeFormat::Float3, .offset = offsetof(Vertex, position)}};
        _staticVariant.pipeline->updateDesc(ci);
    }
}

bool PointShadowIndirectRenderer::hasRenderableInstances(uint32_t flightIndex) const
{
    const auto& flight = _perFlight[flightIndex];
    return _bSupported && _staticVariant.pipeline &&
           flight.activeCullPlugin != ECullPlugin::Disabled &&
           flight.totalInstances > 0;
}

void PointShadowIndirectRenderer::ensureInstanceCapacity(uint32_t requiredCount)
{
    if (requiredCount <= _instanceCapacity) return;

    uint32_t newCap = _instanceCapacity == 0 ? 256u : _instanceCapacity;
    while (newCap < requiredCount) newCap *= 2;
    _instanceCapacity = newCap;

    const uint32_t bufferSize = _instanceCapacity * sizeof(PointShadowInstanceData);
    for (uint32_t i = 0; i < MAX_FLIGHTS_IN_FLIGHT; ++i) {
        auto& flight = _perFlight[i];
        flight.instanceBuffer = IBuffer::create(_render, BufferCreateInfo{
            .label       = std::format("PointShadow_Instance_{}", i),
            .usage       = EBufferUsage::StorageBuffer,
            .size        = bufferSize,
            .memoryUsage = EMemoryUsage::CpuToGpu,
        });
        updateIndirectDescriptors(i);
    }
}

void PointShadowIndirectRenderer::ensureNoCullCommandCapacity(uint32_t faceCount)
{
    if (faceCount <= _noCullFaceCapacity) return;

    _noCullFaceCapacity = faceCount;

    const uint32_t drawCmdSize = ShadowConstants::MAX_DRAWS_PER_FACE * _noCullFaceCapacity * sizeof(PointShadowIndirectCommand);
    const uint32_t drawCountSize = _noCullFaceCapacity * sizeof(uint32_t);
    for (uint32_t i = 0; i < MAX_FLIGHTS_IN_FLIGHT; ++i) {
        auto& flight = _perFlight[i];
        flight.noCullDrawCommandBuffer = IBuffer::create(_render, BufferCreateInfo{
            .label       = std::format("PointShadowNoCull_DrawCmd_{}", i),
            .usage       = EBufferUsage::IndirectBuffer,
            .size        = drawCmdSize,
            .memoryUsage = EMemoryUsage::CpuToGpu,
        });
        flight.noCullDrawCountBuffer = IBuffer::create(_render, BufferCreateInfo{
            .label       = std::format("PointShadowNoCull_DrawCount_{}", i),
            .usage       = EBufferUsage::IndirectBuffer,
            .size        = drawCountSize,
            .memoryUsage = EMemoryUsage::CpuToGpu,
        });
    }
}

void PointShadowIndirectRenderer::uploadNoCullCommands(uint32_t flightIndex, uint32_t faceCount, const std::vector<PointShadowInstanceData>& instanceData)
{
    ensureNoCullCommandCapacity(faceCount);

    auto& flight = _perFlight[flightIndex];
    const uint32_t cappedDrawCount = std::min(static_cast<uint32_t>(instanceData.size()), ShadowConstants::MAX_DRAWS_PER_FACE);

    std::vector<PointShadowIndirectCommand> commands(
        static_cast<size_t>(faceCount) * ShadowConstants::MAX_DRAWS_PER_FACE);
    for (uint32_t face = 0; face < faceCount; ++face) {
        const uint32_t faceBase = face * ShadowConstants::MAX_DRAWS_PER_FACE;
        for (uint32_t instanceIndex = 0; instanceIndex < cappedDrawCount; ++instanceIndex) {
            const auto& instance = instanceData[instanceIndex];
            commands[faceBase + instanceIndex] = PointShadowIndirectCommand{
                .indexCount    = instance.indexCount,
                .instanceCount = 1,
                .firstIndex    = instance.firstIndex,
                .vertexOffset  = instance.vertexOffset,
                .firstInstance = instanceIndex,
            };
        }
    }

    std::vector<uint32_t> counts(faceCount, cappedDrawCount);

    flight.noCullDrawCommandBuffer->writeData(commands.data(), commands.size() * sizeof(PointShadowIndirectCommand), 0);
    flight.noCullDrawCommandBuffer->flush();
    flight.noCullDrawCountBuffer->writeData(counts.data(), counts.size() * sizeof(uint32_t), 0);
    flight.noCullDrawCountBuffer->flush();
}

void PointShadowIndirectRenderer::updateIndirectDescriptors(uint32_t flightIndex)
{
    auto& flight = _perFlight[flightIndex];
    if (!flight.instanceBuffer) return;

    if (!flight.indirectDS) {
        flight.indirectDS = _dsp->allocateDescriptorSets(_indirectDSL);
    }
    _render->getDescriptorHelper()->updateDescriptorSets({
        IDescriptorSetHelper::writeOneStorageBuffer(flight.indirectDS, 0, flight.instanceBuffer.get()),
    });
}

} // namespace ya
