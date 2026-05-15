#pragma once

#include "Runtime/App/Common/Shadow/ShadowTypes.h"

#include "Render/Core/Buffer.h"
#include "Render/Core/DescriptorSet.h"
#include "Render/Core/Pipeline.h"
#include "Render/Stage/IRenderStage.h"

namespace ya
{

struct IRender;
struct ICommandBuffer;

// ═══════════════════════════════════════════════════════════════════════════
// PointShadowCullPass
// GPU frustum culling via compute shader.
// Dispatches one thread per (instance, face) pair; uses atomics to build a
// compacted indirect draw command stream.
// ═══════════════════════════════════════════════════════════════════════════

class PointShadowCullPass
{
  public:
    void init(IRender* render);
    void destroy();

    /// Upload face frustum data and ensure buffer capacity.
    void prepare(uint32_t flightIndex,
                 const PointShadowFaceFrustum* faceFrustums,
                 uint32_t activeFaceCount,
                 uint32_t instanceCount);

    /// Record compute dispatch + barriers.
    void dispatch(ICommandBuffer* cmdBuf, uint32_t flightIndex) const;

    [[nodiscard]] IBuffer* getDrawCommandBuffer(uint32_t flightIndex) const;
    [[nodiscard]] IBuffer* getDrawCountBuffer(uint32_t flightIndex) const;
    [[nodiscard]] uint32_t getActiveFaceCount(uint32_t flightIndex) const;

    /// Bind the external instance buffer to the cull descriptor set (binding 0).
    void bindInstanceBuffer(uint32_t flightIndex, IBuffer* instanceBuffer);

  private:
    using PushConstants = PointShadowCullPushConstant;

    struct PerFlightResources
    {
        stdptr<IBuffer>     faceFrustumBuffer;     // StructuredBuffer<PointShadowFaceFrustum>
        stdptr<IBuffer>     drawCommandBuffer;     // RWStructuredBuffer<IndirectCommand>  (MAX_DRAWS_PER_FACE * faceCount)
        stdptr<IBuffer>     drawCountBuffer;       // RWStructuredBuffer<uint>             (faceCount)
        DescriptorSetHandle cullDS = nullptr;
        uint32_t            activeFaceCount = 0;
        uint32_t            instanceCount   = 0;
    };

    void ensureBufferCapacity(uint32_t faceCount);

    IRender*                     _render = nullptr;
    stdptr<IComputePipeline>     _pipeline;
    stdptr<IPipelineLayout>      _pipelineLayout;
    stdptr<IDescriptorSetLayout> _cullDSL;
    stdptr<IDescriptorPool>      _dsp;

    std::array<PerFlightResources, MAX_FLIGHTS_IN_FLIGHT> _perFlight{};
    uint32_t _allocatedFaceCount = 0; // current buffer capacity in faces
};

} // namespace ya
