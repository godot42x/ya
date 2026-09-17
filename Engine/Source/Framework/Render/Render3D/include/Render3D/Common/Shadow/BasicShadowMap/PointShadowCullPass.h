#pragma once

#include "Render3D/Common/Shadow/BasicShadowMap/PointShadowIndirectResources.h"
#include "Render3D/Common/Shadow/ShadowTypes.h"
#include "Graph/RenderGraph.h"

#include "RHI/Core/Buffer.h"
#include "RHI/Core/DescriptorSet.h"
#include "RHI/Core/Pipeline.h"
#include "Render3D/Stage/IRenderStage.h"

namespace ya
{

struct IRender;
struct ICommandBuffer;

// ═══════════════════════════════════════════════════════════════════════════
// PointShadowCullPass
//
// Device recipe for GPU cull. Instance/cull buffers and descriptor sets live
// on `PointShadowIndirectResources` (one Shadow View Binding), not on a
// flight-indexed table.
// ═══════════════════════════════════════════════════════════════════════════

class PointShadowCullPass
{
  public:
    struct GraphResources
    {
        // The indirect vertex shader reads instance data even when the GPU
        // cull dispatch is disabled. Keep that read in the graph so the
        // View-owned host upload is visible to the raster pass.
        RGBufferHandle              instanceData{};
        RGBufferHandle             drawCommands{};
        RGBufferHandle             visibleInstances{};
        std::optional<RGPassHandle> cullPass{};
    };

    void init(IRender* render);
    void destroy();

    /// Ensure this View packet's (frustum / cmd / lookup) buffers can hold `bucketCount`.
    bool ensureCapacity(PointShadowIndirectResources& packet, uint32_t bucketCount);

    /// Bind the external instance buffer to the View-owned cull DS (binding 0).
    void bindInstanceBuffer(PointShadowIndirectResources& packet, const stdptr<IBuffer>& instanceBuffer);

    /// Compute path — step 1: upload frustums + remember dispatch shape.
    void prepareCompute(PointShadowIndirectResources& packet,
                        const PointShadowFaceFrustum* faceFrustums,
                        uint32_t                      activeFaceCount,
                        uint32_t                      instanceCount,
                        uint32_t                      batchCount);

    /// NoCull path — remember the graph buffer shape for CPU-populated data.
    void prepareNoCull(PointShadowIndirectResources& packet, uint32_t activeFaceCount, uint32_t batchCount);

    /// Both paths — upload the per-bucket cmd template.
    void writeDrawCommandTemplate(PointShadowIndirectResources&     packet,
                                  const PointShadowIndirectCommand* cmds,
                                  uint32_t                          bucketCount);

    /// NoCull path — fill visibleInstances on CPU.
    void writeVisibleInstances(PointShadowIndirectResources& packet,
                               const uint32_t*               data,
                               uint32_t                      count);

    void updateCullDescriptors(PointShadowIndirectResources& packet);

    [[nodiscard]] std::optional<GraphResources> appendGraphPass(
        RenderGraph&                   graph,
        PointShadowIndirectResources&  packet,
        bool                           bDispatchCull,
        std::optional<RGPassHandle>    dependency = std::nullopt);

    [[nodiscard]] stdptr<IDescriptorSetLayout> getCullDSL() const { return _cullDSL; }

  private:
    using PushConstants = PointShadowCullPushConstant;

    IRender*                     _render = nullptr;
    stdptr<IComputePipeline>     _pipeline;
    stdptr<IPipelineLayout>      _pipelineLayout;
    stdptr<IDescriptorSetLayout> _cullDSL;
};

} // namespace ya
