#pragma once

#include "BasicShadowPayload.h"
#include "PointShadowCullPass.h"
#include "Render3D/Common/Shadow/ShadowTypes.h"

#include "RHI/Core/Buffer.h"
#include "RHI/Core/DescriptorSet.h"
#include "RHI/Core/Pipeline.h"

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
// Device recipe (pipeline/layout). Instance lists come from the View draw
// buckets, so GPU packets live on `PointShadowIndirectResources` in the
// Shadow View Binding — not on a flight-indexed table.
// ═══════════════════════════════════════════════════════════════════════════

class PointShadowIndirectRenderer
{
  public:
    void init(IRender* render,
              stdptr<IDescriptorSetLayout> frameDSL);
    void destroy();

    void beginFrame();
    void prepare(const BasicShadowFramePayload& payload);
    void bindGraphVisibleInstances(PointShadowIndirectResources& packet, IBuffer* visibleBuffer);
    void renderFace(ICommandBuffer*                 cmdBuf,
                    const BasicShadowFramePayload&  payload,
                    const PointShadowFacePayload&   facePayload,
                    IBuffer*                        drawCommandBuffer) const;
    void refreshPipeline(EFormat::T depthFormat);

    [[nodiscard]] bool isSupported() const { return _bSupported; }
    [[nodiscard]] bool hasRenderableInstances(const PointShadowIndirectResources* packet) const;
    [[nodiscard]] PointShadowCullPass& getCullPass() { return _cullPass; }
    [[nodiscard]] stdptr<IDescriptorSetLayout> getIndirectDSL() const { return _indirectDSL; }

  private:
    bool collectBatches(const BasicShadowFramePayload&        payload,
                        std::vector<PointShadowInstanceData>& outInstances);
    bool uploadInstances(PointShadowIndirectResources&              packet,
                         const std::vector<PointShadowInstanceData>& instances);
    std::vector<PointShadowIndirectCommand> buildCmdTemplates(const PointShadowIndirectResources& packet) const;
    void fillCullDataCompute(const BasicShadowFramePayload&                 payload,
                             const std::vector<PointShadowIndirectCommand>& cmdTemplates);
    void fillCullDataNoCull(PointShadowIndirectResources&             packet,
                            std::vector<PointShadowIndirectCommand>& cmdTemplates);

    bool ensureInstanceCapacity(PointShadowIndirectResources& packet, uint32_t requiredCount);
    void updateIndirectDescriptors(PointShadowIndirectResources& packet);
    void allocateViewDescriptors(const BasicShadowFramePayload& payload);

    IRender* _render = nullptr;
    bool     _bSupported = false;

    stdptr<IDescriptorSetLayout> _frameDSL;
    stdptr<IDescriptorSetLayout> _indirectDSL;

    stdptr<IGraphicsPipeline>    _pipeline;
    stdptr<IPipelineLayout>      _pipelineLayout;
    GraphicsPipelineCreateInfo   _pipelineCI{};

    PointShadowCullPass _cullPass;
};

} // namespace ya
