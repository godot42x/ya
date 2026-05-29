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
//
// Owns the three GPU buffers shared by both compute-cull path and CPU NoCull
// path:
//   • drawCommandBuffer   [bucketCount]
//   • visibleInstancesBuf [bucketCount * MAX_DRAWS_PER_FACE]
//   • faceFrustumBuffer   [activeFaceCount]      (only used by compute)
//
// Two write modes:
//   1. Compute: prepare() uploads frustums, dispatch() runs the shader,
//      which atomically updates `instanceCount` and appends visible ids.
//   2. NoCull : populateNoCull() writes everything from CPU.
//
// In both cases the vertex shader reads (drawCommandBuffer,
// visibleInstancesBuf) the same way.
// ═══════════════════════════════════════════════════════════════════════════

class PointShadowCullPass
{
  public:
    void init(IRender* render);
    void destroy();

    /// Ensure the (frustum / cmd / lookup) buffers can hold `bucketCount`.
    void ensureCapacity(uint32_t bucketCount);

    /// Bind the external instance buffer to the cull DS (binding 0).
    /// Required only for the compute path; harmless to call always.
    void bindInstanceBuffer(uint32_t flightIndex, IBuffer* instanceBuffer);

    /// Compute path — step 1: upload frustums + remember dispatch shape.
    void prepareCompute(uint32_t flightIndex,
                        const PointShadowFaceFrustum* faceFrustums,
                        uint32_t activeFaceCount,
                        uint32_t instanceCount,
                        uint32_t batchCount);

    /// Both paths — upload the per-bucket cmd template
    /// (indexCount/firstIndex/vertexOffset/firstInstance set by caller).
    /// Compute path leaves instanceCount=0 (cull shader bumps it).
    /// NoCull path passes pre-filled instanceCount.
    void writeDrawCommandTemplate(uint32_t flightIndex,
                                  const PointShadowIndirectCommand* cmds,
                                  uint32_t bucketCount);

    /// NoCull path — fill visibleInstances on CPU.
    void writeVisibleInstances(uint32_t flightIndex,
                               const uint32_t* data,
                               uint32_t count);

    /// Compute path — record dispatch + barriers.
    void dispatch(ICommandBuffer* cmdBuf, uint32_t flightIndex) const;

    [[nodiscard]] IBuffer* getDrawCommandBuffer(uint32_t flightIndex) const;
    [[nodiscard]] IBuffer* getVisibleInstancesBuffer(uint32_t flightIndex) const;

  private:
    using PushConstants = PointShadowCullPushConstant;

    struct PerFlightResources
    {
        stdptr<IBuffer>     faceFrustumBuffer;
        stdptr<IBuffer>     drawCommandBuffer;
        stdptr<IBuffer>     visibleInstancesBuf;
        DescriptorSetHandle cullDS = nullptr;
        // Compute-path dispatch shape
        uint32_t            activeFaceCount  = 0;
        uint32_t            activeBatchCount = 0;
        uint32_t            instanceCount    = 0;
    };

    IRender*                     _render = nullptr;
    stdptr<IComputePipeline>     _pipeline;
    stdptr<IPipelineLayout>      _pipelineLayout;
    stdptr<IDescriptorSetLayout> _cullDSL;
    stdptr<IDescriptorPool>      _dsp;

    std::array<PerFlightResources, MAX_FLIGHTS_IN_FLIGHT> _perFlight{};
    uint32_t _allocatedBucketCount = 0;
};

} // namespace ya
