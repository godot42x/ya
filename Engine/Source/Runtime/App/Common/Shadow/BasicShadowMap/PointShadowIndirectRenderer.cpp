#include "PointShadowIndirectRenderer.h"

#include "Render/Core/CommandBuffer.h"
#include "Render/Mesh.h"
#include "Render/Render.h"
#include "Render/RenderFrameData.h"

#include <algorithm>
#include <format>
#include <unordered_map>
#include <utility>

namespace ya
{

namespace Addr = PointShadowAddressing;

// ════════════════════════════════════════════════════════════════════════
// Init / Destroy / Lifecycle
// ════════════════════════════════════════════════════════════════════════

void PointShadowIndirectRenderer::init(IRender* render, stdptr<IDescriptorSetLayout> frameDSL)
{
    _render   = render;
    _frameDSL = std::move(frameDSL);

    const auto& caps = _render->getCapabilities();
    _bSupported      = caps.computeShader && caps.drawIndexedIndirect && caps.storageBuffer;
    if (!_bSupported) {
        return;
    }

    _indirectDSL = IDescriptorSetLayout::create(
        _render, DescriptorSetLayoutDesc{
                     .label    = "PointShadow_Indirect_DSL",
                     .set      = 1,
                     .bindings = {
                         {.binding = 0, .descriptorType = EPipelineDescriptorType::StorageBuffer, .descriptorCount = 1, .stageFlags = EShaderStage::Vertex},
                         {.binding = 1, .descriptorType = EPipelineDescriptorType::StorageBuffer, .descriptorCount = 1, .stageFlags = EShaderStage::Vertex},
                     },
                 });

    _pipelineLayout = IPipelineLayout::create(
        _render, "PointShadow_Indirect_PPL", {PushConstantRange{.offset = 0, .size = sizeof(VsPushConstants), .stageFlags = EShaderStage::Vertex}}, {_frameDSL, _indirectDSL});

    _pipelineCI = GraphicsPipelineCreateInfo{
        .pipelineRenderingInfo = {.label = "Point Shadow Indirect", .depthAttachmentFormat = EFormat::D32_SFLOAT},
        .pipelineLayout        = _pipelineLayout.get(),
        .shaderDesc            = ShaderDesc{
            .shaderName        = "Shadow/PointShadowIndirect.slang",
            .vertexBufferDescs = {VertexBufferDescription{.slot = 0, .pitch = sizeof(Vertex)}},
            .vertexAttributes  = {{.bufferSlot = 0, .location = 0, .format = EVertexAttributeFormat::Float3, .offset = offsetof(Vertex, position)}},
        },
        .dynamicFeatures    = {EPipelineDynamicFeature::Viewport, EPipelineDynamicFeature::Scissor},
        .primitiveType      = EPrimitiveType::TriangleList,
        .rasterizationState = {.polygonMode = EPolygonMode::Fill, .cullMode = ECullMode::Front, .frontFace = EFrontFaceType::CounterClockWise},
        .depthStencilState  = {.bDepthTestEnable = true, .bDepthWriteEnable = true, .depthCompareOp = ECompareOp::LessOrEqual},
        .colorBlendState    = {.attachments = {}},
        .viewportState      = {.viewports = {Viewport::defaults()}, .scissors = {Scissor::defaults()}},
    };

    _pipeline = IGraphicsPipeline::create(_render);
    YA_CORE_ASSERT(_pipeline && _pipeline->recreate(_pipelineCI),
                   "Failed to create point shadow indirect pipeline");

    _dsp = IDescriptorPool::create(
        _render,
        DescriptorPoolCreateInfo{
            .label     = "PointShadowIndirect_DSP",
            .maxSets   = MAX_FLIGHTS_IN_FLIGHT,
            .poolSizes = {{.type = EPipelineDescriptorType::StorageBuffer, .descriptorCount = MAX_FLIGHTS_IN_FLIGHT * 2}},
        });

    _cullPass.init(_render);
}

void PointShadowIndirectRenderer::destroy()
{
    _cullPass.destroy();
    for (auto& flight : _perFlight) {
        flight.instanceBuffer.reset();
        flight.indirectDS = nullptr;
        flight.meshBatches.clear();
        flight = {};
    }
    _dsp.reset();
    _pipeline.reset();
    _pipelineLayout.reset();
    _indirectDSL.reset();
    _frameDSL.reset();
    _instanceCapacity = 0;
    _bSupported       = false;
    _render           = nullptr;
}

void PointShadowIndirectRenderer::beginFrame()
{
    if (_pipeline) _pipeline->beginFrame();
}

// ════════════════════════════════════════════════════════════════════════
// prepare() — orchestrates 4 sub-steps
// ════════════════════════════════════════════════════════════════════════

void PointShadowIndirectRenderer::prepare(const BasicShadowFramePayload& payload)
{
    auto& flight = _perFlight[payload.flightIndex];
    flight       = {}; // reset per-frame state

    if (!_bSupported || !payload.pointIndirectRequested() ||
        !payload.frameData || payload.pointLightCount == 0) {
        return;
    }
    flight.useGpuCull      = payload.pointIndirectCullEnabled();
    flight.activeFaceCount = payload.pointLightCount * ShadowConstants::FACES_PER_POINT_LIGHT;

    // ── Step 1: collect batches & build instance array ──────────────
    std::vector<PointShadowInstanceData> instances;
    if (!collectBatches(payload, instances)) return;

    // ── Step 2: upload instance buffer ──────────────────────────────
    uploadInstances(payload.flightIndex, instances);

    // ── Step 3: build cmd templates (1 cmd / bucket) ────────────────
    auto cmdTemplates = buildCmdTemplates(payload.flightIndex);

    const uint32_t bucketCount = static_cast<uint32_t>(cmdTemplates.size());
    _cullPass.ensureCapacity(bucketCount);

    // ── Step 4: fill the cull data — either via GPU dispatch or CPU ─
    if (flight.useGpuCull) {
        fillCullDataCompute(payload, cmdTemplates);
    }
    else {
        fillCullDataNoCull(payload.flightIndex, cmdTemplates);
    }

    updateIndirectDescriptors(payload.flightIndex);
    flight.ready = true;
}

bool PointShadowIndirectRenderer::collectBatches(const BasicShadowFramePayload&        payload,
                                                 std::vector<PointShadowInstanceData>& outInstances)
{
    auto& flight = _perFlight[payload.flightIndex];

    // 1. Group draw items by mesh.
    struct Pending
    {
        Mesh*                       mesh = nullptr;
        std::vector<RenderDrawItem> items;
    };
    std::vector<Pending>                pending;
    std::unordered_map<Mesh*, uint32_t> meshToBatch;
    pending.reserve(16);

    auto pushItems = [&](const std::vector<RenderDrawItem>& items)
    {
        for (const auto& item : items) {
            if (!item.mesh) continue;
            auto [it, inserted] = meshToBatch.try_emplace(item.mesh, static_cast<uint32_t>(pending.size()));
            if (inserted) pending.push_back(Pending{.mesh = item.mesh});
            pending[it->second].items.push_back(item);
        }
    };
    const auto& s = payload.frameData->drawBuckets.staticMeshes;
    pushItems(s.pbrDrawItems);
    pushItems(s.phongDrawItems);
    pushItems(s.unlitDrawItems);
    pushItems(s.simpleDrawItems);
    pushItems(s.fallbackDrawItems);

    if (pending.empty()) return false;

    // 2. Flatten to ShadowInstanceData in batch order, recording each batch's range.
    flight.meshBatches.reserve(pending.size());
    for (uint32_t batchIdx = 0; batchIdx < pending.size(); ++batchIdx) {
        const auto&    p             = pending[batchIdx];
        const uint32_t firstInstance = static_cast<uint32_t>(outInstances.size());

        for (const auto& item : p.items) {
            const auto      bb     = item.mesh->boundingBox.transformed(item.worldMatrix);
            const glm::vec3 center = 0.5f * (bb.min + bb.max);
            const float     radius = glm::length(bb.max - center);

            outInstances.push_back(PointShadowInstanceData{
                .worldMatrix    = item.worldMatrix,
                .boundingSphere = glm::vec4(center, radius),
                .batchIndex     = batchIdx,
            });
        }
        flight.meshBatches.push_back(MeshBatch{
            .mesh          = p.mesh,
            .firstInstance = firstInstance,
            .instanceCount = static_cast<uint32_t>(p.items.size()),
        });
    }
    flight.totalInstances = static_cast<uint32_t>(outInstances.size());
    return flight.totalInstances > 0;
}

void PointShadowIndirectRenderer::uploadInstances(uint32_t                                    flightIndex,
                                                  const std::vector<PointShadowInstanceData>& instances)
{
    ensureInstanceCapacity(static_cast<uint32_t>(instances.size()));
    auto& flight = _perFlight[flightIndex];
    flight.instanceBuffer->writeData(instances.data(), instances.size() * sizeof(PointShadowInstanceData), 0);
    flight.instanceBuffer->flush();
}

std::vector<PointShadowIndirectCommand>
PointShadowIndirectRenderer::buildCmdTemplates(uint32_t flightIndex) const
{
    const auto&    flight     = _perFlight[flightIndex];
    const uint32_t batchCount = static_cast<uint32_t>(flight.meshBatches.size());
    const uint32_t faceCount  = flight.activeFaceCount;

    std::vector<PointShadowIndirectCommand> cmds(batchCount * faceCount);
    for (uint32_t batch = 0; batch < batchCount; ++batch) {
        const Mesh*    mesh       = flight.meshBatches[batch].mesh;
        const uint32_t indexCount = mesh ? mesh->getIndexCount() : 0;
        for (uint32_t face = 0; face < faceCount; ++face) {
            cmds[Addr::bucketIndex(batch, face, faceCount)] = PointShadowIndirectCommand{
                .indexCount    = indexCount,
                .instanceCount = 0, // populated by cull or NoCull below
                .firstIndex    = 0,
                .vertexOffset  = 0,
                .firstInstance = 0,
            };
        }
    }
    return cmds;
}

void PointShadowIndirectRenderer::fillCullDataCompute(const BasicShadowFramePayload&                 payload,
                                                      const std::vector<PointShadowIndirectCommand>& cmdTemplates)
{
    auto&          flight     = _perFlight[payload.flightIndex];
    const uint32_t batchCount = static_cast<uint32_t>(flight.meshBatches.size());
    const uint32_t faceCount  = flight.activeFaceCount;

    _cullPass.bindInstanceBuffer(payload.flightIndex, flight.instanceBuffer.get());
    _cullPass.writeDrawCommandTemplate(payload.flightIndex, cmdTemplates.data(), static_cast<uint32_t>(cmdTemplates.size()));

    // Build face frustums (light × 6).
    std::vector<PointShadowFaceFrustum> frustums(faceCount);
    for (uint32_t light = 0; light < payload.pointLightCount; ++light) {
        for (uint32_t f = 0; f < ShadowConstants::FACES_PER_POINT_LIGHT; ++f) {
            const auto planes = extractFrustumPlanes(payload.frameUBO.pointLights[light].matrix[f]);
            auto&      fr     = frustums[light * ShadowConstants::FACES_PER_POINT_LIGHT + f];
            for (uint32_t p = 0; p < 6; ++p) fr.planes[p] = planes[p];
        }
    }
    _cullPass.prepareCompute(payload.flightIndex, frustums.data(), faceCount, flight.totalInstances, batchCount);
}

void PointShadowIndirectRenderer::fillCullDataNoCull(uint32_t                                 flightIndex,
                                                     std::vector<PointShadowIndirectCommand>& cmdTemplates)
{
    auto&          flight     = _perFlight[flightIndex];
    const uint32_t batchCount = static_cast<uint32_t>(flight.meshBatches.size());
    const uint32_t faceCount  = flight.activeFaceCount;

    // Pre-fill instanceCount + visibleInstances ourselves (cull shader skipped).
    std::vector<uint32_t> visible(static_cast<size_t>(cmdTemplates.size()) * ShadowConstants::MAX_DRAWS_PER_FACE, 0u);

    for (uint32_t batch = 0; batch < batchCount; ++batch) {
        const auto&    mb       = flight.meshBatches[batch];
        const uint32_t capCount = std::min(mb.instanceCount, ShadowConstants::MAX_DRAWS_PER_FACE);
        for (uint32_t face = 0; face < faceCount; ++face) {
            const uint32_t bucket              = Addr::bucketIndex(batch, face, faceCount);
            cmdTemplates[bucket].instanceCount = capCount;
            const uint32_t base                = Addr::bucketBaseSlot(bucket);
            for (uint32_t slot = 0; slot < capCount; ++slot) {
                visible[base + slot] = mb.firstInstance + slot;
            }
        }
    }

    _cullPass.writeDrawCommandTemplate(flightIndex, cmdTemplates.data(), static_cast<uint32_t>(cmdTemplates.size()));
    _cullPass.writeVisibleInstances(flightIndex, visible.data(), static_cast<uint32_t>(visible.size()));
}

// ════════════════════════════════════════════════════════════════════════
// GPU recording
// ════════════════════════════════════════════════════════════════════════

void PointShadowIndirectRenderer::dispatchCull(ICommandBuffer* cmdBuf, uint32_t flightIndex) const
{
    const auto& flight = _perFlight[flightIndex];
    if (!flight.ready || !flight.useGpuCull) return;
    _cullPass.dispatch(cmdBuf, flightIndex);
}

void PointShadowIndirectRenderer::renderFace(ICommandBuffer*                cmdBuf,
                                             const BasicShadowFramePayload& payload,
                                             const PointShadowFacePayload&  facePayload) const
{
    const auto& flight = _perFlight[payload.flightIndex];
    if (!flight.ready) return;

    cmdBuf->bindPipeline(_pipeline.get());
    cmdBuf->bindDescriptorSets(_pipelineLayout.get(), 0, {facePayload.faceDS, flight.indirectDS});

    IBuffer*       cmdBuffer  = _cullPass.getDrawCommandBuffer(payload.flightIndex);
    const uint32_t faceCount  = flight.activeFaceCount;
    const uint32_t face       = facePayload.faceGlobalIndex;
    const uint32_t batchCount = static_cast<uint32_t>(flight.meshBatches.size());

    // One indirect draw per (batch, this face) bucket.
    for (uint32_t batch = 0; batch < batchCount; ++batch) {
        const auto& mb = flight.meshBatches[batch];
        if (!mb.mesh) continue;

        cmdBuf->bindVertexBuffer(0, mb.mesh->getVertexBuffer(), mb.mesh->getVertexBufferOffset());
        cmdBuf->bindIndexBuffer(mb.mesh->getIndexBufferMut(), mb.mesh->getIndexBufferOffset(), false);

        const uint32_t bucket    = Addr::bucketIndex(batch, face, faceCount);
        const uint64_t cmdOffset = static_cast<uint64_t>(bucket) * sizeof(PointShadowIndirectCommand);

        VsPushConstants pc{.bucketBase = Addr::bucketBaseSlot(bucket)};
        cmdBuf->pushConstants(_pipelineLayout.get(), EShaderStage::Vertex, 0, sizeof(pc), &pc);
        cmdBuf->drawIndexedIndirect(cmdBuffer, cmdOffset, 1, sizeof(PointShadowIndirectCommand));
    }
}

void PointShadowIndirectRenderer::refreshPipeline(EFormat::T depthFormat)
{
    _pipelineCI.pipelineRenderingInfo.depthAttachmentFormat = depthFormat;
    if (_pipeline) _pipeline->updateDesc(_pipelineCI);
}

bool PointShadowIndirectRenderer::hasRenderableInstances(uint32_t flightIndex) const
{
    return _bSupported && _pipeline && _perFlight[flightIndex].ready;
}

// ════════════════════════════════════════════════════════════════════════
// Helpers
// ════════════════════════════════════════════════════════════════════════

void PointShadowIndirectRenderer::ensureInstanceCapacity(uint32_t requiredCount)
{
    if (requiredCount <= _instanceCapacity) return;

    uint32_t newCap = _instanceCapacity == 0 ? 256u : _instanceCapacity;
    while (newCap < requiredCount) newCap *= 2;
    _instanceCapacity = newCap;

    const uint32_t bufferSize = _instanceCapacity * static_cast<uint32_t>(sizeof(PointShadowInstanceData));
    for (uint32_t i = 0; i < MAX_FLIGHTS_IN_FLIGHT; ++i) {
        _perFlight[i].instanceBuffer = IBuffer::create(
            _render,
            BufferCreateInfo{
                .label       = std::format("PointShadow_Instance_{}", i),
                .usage       = EBufferUsage::StorageBuffer,
                .size        = bufferSize,
                .memoryUsage = EMemoryUsage::CpuToGpu,
            });
    }
}

void PointShadowIndirectRenderer::updateIndirectDescriptors(uint32_t flightIndex)
{
    auto&    flight        = _perFlight[flightIndex];
    IBuffer* visibleBuffer = _cullPass.getVisibleInstancesBuffer(flightIndex);
    if (!flight.instanceBuffer || !visibleBuffer) return;

    if (!flight.indirectDS) flight.indirectDS = _dsp->allocateDescriptorSets(_indirectDSL);
    _render->getDescriptorHelper()->updateDescriptorSets({
        IDescriptorSetHelper::writeOneStorageBuffer(flight.indirectDS, 0, flight.instanceBuffer.get()),
        IDescriptorSetHelper::writeOneStorageBuffer(flight.indirectDS, 1, visibleBuffer),
    });
}

} // namespace ya
