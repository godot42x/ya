#pragma once

#include "RHI/Core/Buffer.h"
#include "RHI/Core/DescriptorSet.h"

#include <cstdint>
#include <vector>

namespace ya
{

struct Mesh;

/// CPU mesh batch produced while preparing one View's point-shadow packet.
struct PointShadowMeshBatch
{
    Mesh*    mesh          = nullptr;
    uint32_t firstInstance = 0;
    uint32_t instanceCount = 0;
};

/// Point-shadow instance/cull GPU packet owned by one Shadow View Binding.
///
/// Instance lists come from that View's draw buckets, so two Views cannot
/// share the packet just because they share a flight. Descriptor sets wrap
/// these buffers and are allocated from the live RenderSubmission.
struct PointShadowIndirectResources
{
    stdptr<IBuffer>     instanceBuffer;
    uint32_t            instanceCapacity = 0;
    DescriptorSetHandle indirectDS{};

    stdptr<IBuffer>     faceFrustumUploadBuffer;
    stdptr<IBuffer>     faceFrustumExecBuffer;
    stdptr<IBuffer>     drawCommandUploadBuffer;
    stdptr<IBuffer>     drawCommandExecBuffer;
    stdptr<IBuffer>     visibleInstancesUploadBuffer;
    stdptr<IBuffer>     visibleInstancesExecBuffer;
    stdptr<IBuffer>     cullInstanceBuffer;
    DescriptorSetHandle cullDS{};
    uint32_t            allocatedBucketCount = 0;
    uint32_t            activeFaceCount      = 0;
    uint32_t            activeBatchCount     = 0;
    uint32_t            cullInstanceCount    = 0;
    uint32_t            totalInstances       = 0;
    bool                useGpuCull           = false;
    bool                ready                = false;
    std::vector<PointShadowMeshBatch> meshBatches;
};

} // namespace ya
