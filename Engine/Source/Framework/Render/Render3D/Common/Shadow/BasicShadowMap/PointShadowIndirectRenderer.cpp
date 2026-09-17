#include "Render3D/Common/Shadow/BasicShadowMap/PointShadowIndirectRenderer.h"

#include "Core/Profiling/Instrumentor.h"
#include "Core/Common/DeferredDeletionQueue.h"

#include "RHI/Core/RenderResourceFactory.h"
#include "Resource/Mesh.h"
#include "RHI/Render.h"
#include "Render3D/RenderFrameData.h"
#include "Render3D/Common/RenderSubmission.h"
#include "Render3D/Common/Shadow/BasicShadowMap/PointShadowBufferUtils.h"

#include <algorithm>
#include <limits>
#include <unordered_map>
#include <utility>

namespace ya
{

namespace Addr = PointShadowAddressing;

void PointShadowIndirectRenderer::init(IRender* render,
                                       stdptr<IDescriptorSetLayout> frameDSL)
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
        _render, "PointShadow_Indirect_PPL", {}, {_frameDSL, _indirectDSL});

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

    _cullPass.init(_render);
}

void PointShadowIndirectRenderer::destroy()
{
    _cullPass.destroy();
    _pipeline.reset();
    _pipelineLayout.reset();
    _indirectDSL.reset();
    _frameDSL.reset();
    _bSupported = false;
    _render     = nullptr;
}

void PointShadowIndirectRenderer::beginFrame()
{
    if (_pipeline) _pipeline->beginFrame();
}

void PointShadowIndirectRenderer::prepare(const BasicShadowFramePayload& payload)
{
    YA_PROFILE_FUNCTION();
    if (!payload.pointShadow) {
        return;
    }
    auto& packet = *payload.pointShadow;
    packet.meshBatches.clear();
    packet.totalInstances  = 0;
    packet.activeFaceCount = 0;
    packet.useGpuCull      = false;
    packet.ready           = false;
    packet.indirectDS      = {};
    packet.cullDS          = {};

    if (!_bSupported || !payload.pointIndirectRequested() ||
        !payload.frameData || payload.pointLightCount == 0) {
        return;
    }
    packet.useGpuCull      = payload.pointIndirectCullEnabled();
    packet.activeFaceCount = payload.pointLightCount * ShadowConstants::FACES_PER_POINT_LIGHT;

    std::vector<PointShadowInstanceData> instances;
    if (!collectBatches(payload, instances)) return;

    if (!uploadInstances(packet, instances)) return;

    auto cmdTemplates = buildCmdTemplates(packet);

    const uint32_t bucketCount = static_cast<uint32_t>(cmdTemplates.size());
    if (!_cullPass.ensureCapacity(packet, bucketCount)) return;

    if (packet.useGpuCull) {
        fillCullDataCompute(payload, cmdTemplates);
    }
    else {
        fillCullDataNoCull(packet, cmdTemplates);
    }

    allocateViewDescriptors(payload);
    _cullPass.bindInstanceBuffer(packet, packet.instanceBuffer);
    _cullPass.updateCullDescriptors(packet);
    updateIndirectDescriptors(packet);
    packet.ready = true;
}

bool PointShadowIndirectRenderer::collectBatches(const BasicShadowFramePayload&        payload,
                                                 std::vector<PointShadowInstanceData>& outInstances)
{
    YA_PROFILE_FUNCTION();
    auto& packet = *payload.pointShadow;

    struct Pending
    {
        Mesh*                       mesh = nullptr;
        std::vector<RenderDrawItem> items;
    };
    std::vector<Pending>                pending;
    std::unordered_map<Mesh*, uint32_t> meshToBatch;
    pending.reserve(16);

    auto pushItems = [&](DrawCandidateView items)
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

    packet.meshBatches.reserve(pending.size());
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
        packet.meshBatches.push_back(PointShadowMeshBatch{
            .mesh          = p.mesh,
            .firstInstance = firstInstance,
            .instanceCount = static_cast<uint32_t>(p.items.size()),
        });
    }
    packet.totalInstances = static_cast<uint32_t>(outInstances.size());
    return packet.totalInstances > 0;
}

bool PointShadowIndirectRenderer::uploadInstances(PointShadowIndirectResources&              packet,
                                                  const std::vector<PointShadowInstanceData>& instances)
{
    YA_PROFILE_FUNCTION();
    if (!ensureInstanceCapacity(packet, static_cast<uint32_t>(instances.size()))) return false;
    if (!packet.instanceBuffer) return false;
    const uint64_t bytes = static_cast<uint64_t>(instances.size()) * sizeof(PointShadowInstanceData);
    if (bytes > std::numeric_limits<uint32_t>::max() ||
        !packet.instanceBuffer->writeData(instances.data(), static_cast<uint32_t>(bytes), 0) ||
        !packet.instanceBuffer->flush(static_cast<uint32_t>(bytes), 0)) {
        return false;
    }
    return true;
}

std::vector<PointShadowIndirectCommand> PointShadowIndirectRenderer::buildCmdTemplates(
    const PointShadowIndirectResources& packet) const
{
    YA_PROFILE_FUNCTION();
    const uint32_t batchCount = static_cast<uint32_t>(packet.meshBatches.size());
    const uint32_t faceCount  = packet.activeFaceCount;

    std::vector<PointShadowIndirectCommand> cmds(batchCount * faceCount);
    for (uint32_t batch = 0; batch < batchCount; ++batch) {
        const Mesh*    mesh       = packet.meshBatches[batch].mesh;
        const uint32_t indexCount = mesh ? mesh->getIndexCount() : 0;
        for (uint32_t face = 0; face < faceCount; ++face) {
            cmds[Addr::bucketIndex(batch, face, faceCount)] = PointShadowIndirectCommand{
                .indexCount    = indexCount,
                .instanceCount = 0,
                .firstIndex    = 0,
                .vertexOffset  = 0,
                .firstInstance = Addr::bucketBaseSlot(Addr::bucketIndex(batch, face, faceCount)),
            };
        }
    }
    return cmds;
}

void PointShadowIndirectRenderer::fillCullDataCompute(const BasicShadowFramePayload&                 payload,
                                                      const std::vector<PointShadowIndirectCommand>& cmdTemplates)
{
    YA_PROFILE_FUNCTION();
    auto&          packet     = *payload.pointShadow;
    const uint32_t batchCount = static_cast<uint32_t>(packet.meshBatches.size());
    const uint32_t faceCount  = packet.activeFaceCount;

    _cullPass.writeDrawCommandTemplate(packet, cmdTemplates.data(), static_cast<uint32_t>(cmdTemplates.size()));

    std::vector<PointShadowFaceFrustum> frustums(faceCount);
    for (uint32_t light = 0; light < payload.pointLightCount; ++light) {
        for (uint32_t f = 0; f < ShadowConstants::FACES_PER_POINT_LIGHT; ++f) {
            const auto planes = extractFrustumPlanes(payload.frameUBO.pointLights[light].matrix[f]);
            auto&      fr     = frustums[light * ShadowConstants::FACES_PER_POINT_LIGHT + f];
            for (uint32_t p = 0; p < 6; ++p) fr.planes[p] = planes[p];
        }
    }
    _cullPass.prepareCompute(packet, frustums.data(), faceCount, packet.totalInstances, batchCount);
}

void PointShadowIndirectRenderer::fillCullDataNoCull(PointShadowIndirectResources&             packet,
                                                     std::vector<PointShadowIndirectCommand>& cmdTemplates)
{
    YA_PROFILE_FUNCTION();
    const uint32_t batchCount = static_cast<uint32_t>(packet.meshBatches.size());
    const uint32_t faceCount  = packet.activeFaceCount;

    _cullPass.prepareNoCull(packet, faceCount, batchCount);

    std::vector<uint32_t> visible(static_cast<size_t>(cmdTemplates.size()) * ShadowConstants::MAX_DRAWS_PER_FACE, 0u);

    for (uint32_t batch = 0; batch < batchCount; ++batch) {
        const auto&    mb       = packet.meshBatches[batch];
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

    _cullPass.writeDrawCommandTemplate(packet, cmdTemplates.data(), static_cast<uint32_t>(cmdTemplates.size()));
    _cullPass.writeVisibleInstances(packet, visible.data(), static_cast<uint32_t>(visible.size()));
}

void PointShadowIndirectRenderer::bindGraphVisibleInstances(PointShadowIndirectResources& packet, IBuffer* visibleBuffer)
{
    if (!visibleBuffer || !_render || !packet.indirectDS || !packet.instanceBuffer) return;
    _render->getDescriptorHelper()->updateDescriptorSets({
        IDescriptorSetHelper::writeOneStorageBuffer(packet.indirectDS, 0, packet.instanceBuffer.get()),
        IDescriptorSetHelper::writeOneStorageBuffer(packet.indirectDS, 1, visibleBuffer),
    });
}

void PointShadowIndirectRenderer::renderFace(ICommandBuffer*                cmdBuf,
                                             const BasicShadowFramePayload& payload,
                                             const PointShadowFacePayload&  facePayload,
                                             IBuffer*                       drawCommandBuffer) const
{
    YA_PROFILE_FUNCTION();
    if (!payload.pointShadow || !payload.pointShadow->ready) return;
    const auto& packet = *payload.pointShadow;

    cmdBuf->bindPipeline(_pipeline.get());
    cmdBuf->bindDescriptorSets(_pipelineLayout.get(), 0, {facePayload.faceDS, packet.indirectDS});

    IBuffer* cmdBuffer = drawCommandBuffer;
    if (!cmdBuffer) return;
    const uint32_t faceCount  = packet.activeFaceCount;
    const uint32_t face       = facePayload.faceGlobalIndex;
    const uint32_t batchCount = static_cast<uint32_t>(packet.meshBatches.size());

    for (uint32_t batch = 0; batch < batchCount; ++batch) {
        const auto& mb = packet.meshBatches[batch];
        if (!mb.mesh) continue;

        cmdBuf->bindVertexBuffer(0, mb.mesh->getVertexBuffer(), mb.mesh->getVertexBufferOffset());
        cmdBuf->bindIndexBuffer(mb.mesh->getIndexBufferMut(), mb.mesh->getIndexBufferOffset(), false);

        const uint32_t bucket    = Addr::bucketIndex(batch, face, faceCount);
        const uint64_t cmdOffset = static_cast<uint64_t>(bucket) * sizeof(PointShadowIndirectCommand);

        cmdBuf->drawIndexedIndirect(cmdBuffer, cmdOffset, 1, sizeof(PointShadowIndirectCommand));
    }
}

void PointShadowIndirectRenderer::refreshPipeline(EFormat::T depthFormat)
{
    _pipelineCI.pipelineRenderingInfo.depthAttachmentFormat = depthFormat;
    if (_pipeline) _pipeline->updateDesc(_pipelineCI);
}

bool PointShadowIndirectRenderer::hasRenderableInstances(const PointShadowIndirectResources* packet) const
{
    return _bSupported && _pipeline && packet && packet->ready;
}

void PointShadowIndirectRenderer::allocateViewDescriptors(const BasicShadowFramePayload& payload)
{
    if (!payload.submission || !payload.pointShadow || !payload.submission->isRecording()) {
        return;
    }
    auto& packet = *payload.pointShadow;
    packet.indirectDS = payload.submission->allocateDescriptorSet(
        _indirectDSL, 2, EPipelineDescriptorType::StorageBuffer);
    packet.cullDS = payload.submission->allocateDescriptorSet(
        _cullPass.getCullDSL(), 4, EPipelineDescriptorType::StorageBuffer);
}

bool PointShadowIndirectRenderer::ensureInstanceCapacity(PointShadowIndirectResources& packet, uint32_t requiredCount)
{
    if (requiredCount <= packet.instanceCapacity && packet.instanceBuffer) return true;

    const uint32_t maxInstanceCount = std::numeric_limits<uint32_t>::max() / sizeof(PointShadowInstanceData);
    if (requiredCount > maxInstanceCount) {
        YA_CORE_ERROR("Point shadow instance count {} exceeds buffer size limit", requiredCount);
        return false;
    }
    uint32_t newCap = packet.instanceCapacity == 0 ? 256u : packet.instanceCapacity;
    while (newCap < requiredCount) {
        if (newCap > maxInstanceCount / 2u) {
            newCap = requiredCount;
            break;
        }
        newCap *= 2u;
    }

    const uint64_t bufferSize64 = static_cast<uint64_t>(newCap) * sizeof(PointShadowInstanceData);
    if (bufferSize64 > std::numeric_limits<uint32_t>::max()) {
        YA_CORE_ERROR("Point shadow instance buffer for {} entries exceeds 32-bit size", newCap);
        return false;
    }
    auto nextBuffer = createPointShadowBuffer(_render,
                                              "PointShadow_Instance",
                                              EBufferUsage::StorageBuffer,
                                              static_cast<uint32_t>(bufferSize64),
                                              EMemoryUsage::CpuToGpu);
    if (!nextBuffer || !nextBuffer->getHandle()) {
        YA_CORE_ERROR("Failed to create point shadow indirect instance buffer");
        return false;
    }

    auto oldBuffer = std::move(packet.instanceBuffer);
    packet.instanceBuffer   = std::move(nextBuffer);
    packet.instanceCapacity = newCap;
    DeferredDeletionQueue::get().retire(std::move(oldBuffer));
    return true;
}

void PointShadowIndirectRenderer::updateIndirectDescriptors(PointShadowIndirectResources& packet)
{
    YA_PROFILE_FUNCTION();
    IBuffer* visibleBuffer = packet.visibleInstancesExecBuffer.get();
    if (!packet.instanceBuffer || !visibleBuffer || !packet.indirectDS || !_render) return;

    _render->getDescriptorHelper()->updateDescriptorSets({
        IDescriptorSetHelper::writeOneStorageBuffer(packet.indirectDS, 0, packet.instanceBuffer.get()),
        IDescriptorSetHelper::writeOneStorageBuffer(packet.indirectDS, 1, visibleBuffer),
    });
}

} // namespace ya
