#pragma once

#include "BasicShadowPayload.h"
#include "PointShadowCullPass.h"
#include "Runtime/Rendering/Common/Shadow/ShadowTypes.h"

#include "Render/Core/Buffer.h"
#include "Render/Core/DescriptorSet.h"
#include "Render/Core/Pipeline.h"

#include <array>
#include <vector>

namespace ya
{

struct IRender;
struct ICommandBuffer;
struct IDescriptorSetLayout;
struct Mesh;
struct RenderDrawItem;

// ═══════════════════════════════════════════════════════════════════════════
// PointShadowIndirectRenderer
//
// Frame flow (see ShadowTypes.h for the bucket addressing convention):
//
//   1. prepare()       — CPU
//        a. collectBatches()   : group draw items by mesh → batches
//        b. uploadInstances()  : flatten to ShadowInstanceData[]
//        c. uploadCmds()       : write 1 cmd template per (batch,face) bucket
//        d. fillCullData()     : either upload frustums (compute path) or
//                                 fill instanceCount + visibleInstances (NoCull)
//
//   2. dispatchCull()  — GPU compute (compute path only)
//        atomically increments cmd.instanceCount and appends visible ids.
//
//   3. renderFace()    — GPU graphics, called once per (light, cube face)
//        for each batch:
//          bind VB/IB → drawIndexedIndirect(1 cmd)
//        VS reads visibleInstances[bucketBase + SV_InstanceID], where
//        bucketBase rides the indirect command's firstInstance field.
// ═══════════════════════════════════════════════════════════════════════════

class PointShadowIndirectRenderer
{
  public:
    void init(IRender* render,
              stdptr<IDescriptorSetLayout> frameDSL);
    void destroy();

    void beginFrame();
    void prepare(const BasicShadowFramePayload& payload);
    void bindGraphVisibleInstances(uint32_t flightIndex, IBuffer* visibleBuffer);
    void renderFace(ICommandBuffer*                cmdBuf,
                    const BasicShadowFramePayload& payload,
                    const PointShadowFacePayload&  facePayload,
                    IBuffer*                        drawCommandBuffer) const;
    void refreshPipeline(EFormat::T depthFormat);

    [[nodiscard]] bool isSupported() const { return _bSupported; }
    [[nodiscard]] bool hasRenderableInstances(uint32_t flightIndex) const;
    [[nodiscard]] PointShadowCullPass& getCullPass() { return _cullPass; }

  private:
    // ─── Per-frame state ─────────────────────────────────────────────
    struct MeshBatch
    {
        Mesh*    mesh          = nullptr;
        uint32_t firstInstance = 0;   // base offset into instanceData
        uint32_t instanceCount = 0;   // how many entries in this batch
    };

    struct PerFlightResources
    {
        stdptr<IBuffer>     instanceBuffer;   // ShadowInstanceData[]
        DescriptorSetHandle indirectDS = nullptr;
        uint32_t            instanceCapacity = 0;

        std::vector<MeshBatch> meshBatches;
        uint32_t               totalInstances  = 0;
        uint32_t               activeFaceCount = 0;
        bool                   useGpuCull      = false;
        bool                   ready           = false;
    };

    // ─── prepare() sub-steps (all operate on _perFlight[payload.flightIndex]) ─
    bool collectBatches(const BasicShadowFramePayload& payload,
                        std::vector<PointShadowInstanceData>& outInstances);
    bool uploadInstances(uint32_t flightIndex,
                         const std::vector<PointShadowInstanceData>& instances);
    std::vector<PointShadowIndirectCommand> buildCmdTemplates(uint32_t flightIndex) const;
    void fillCullDataCompute(const BasicShadowFramePayload&                 payload,
                             const std::vector<PointShadowIndirectCommand>& cmdTemplates);
    void fillCullDataNoCull(uint32_t                                       flightIndex,
                            std::vector<PointShadowIndirectCommand>&       cmdTemplates);

    // ─── Capacity & DS helpers ───────────────────────────────────────
    bool ensureInstanceCapacity(uint32_t flightIndex, uint32_t requiredCount);
    void updateIndirectDescriptors(uint32_t flightIndex);

    // ─── Resources ───────────────────────────────────────────────────
    IRender* _render = nullptr;
    bool     _bSupported = false;

    stdptr<IDescriptorSetLayout> _frameDSL;
    stdptr<IDescriptorSetLayout> _indirectDSL;
    stdptr<IDescriptorPool>      _dsp;

    stdptr<IGraphicsPipeline>    _pipeline;
    stdptr<IPipelineLayout>      _pipelineLayout;
    GraphicsPipelineCreateInfo   _pipelineCI{};

    PointShadowCullPass _cullPass;

    std::array<PerFlightResources, MAX_FLIGHTS_IN_FLIGHT> _perFlight{};
};

} // namespace ya
