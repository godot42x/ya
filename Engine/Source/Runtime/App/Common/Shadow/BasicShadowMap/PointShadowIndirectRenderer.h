#pragma once

#include "BasicShadowPayload.h"
#include "PointShadowCullPass.h"
#include "Runtime/App/Common/Shadow/ShadowTypes.h"

#include "Render/Core/Buffer.h"
#include "Render/Core/DescriptorSet.h"
#include "Render/Core/Pipeline.h"

#include <array>
#include <unordered_map>
#include <vector>

namespace ya
{

struct IRender;
struct ICommandBuffer;
struct IDescriptorSetLayout;
struct Mesh;
struct RenderDrawItem;
struct RenderShadingDrawBuckets;

class PointShadowIndirectRenderer
{
  public:
    void init(IRender* render, stdptr<IDescriptorSetLayout> frameDSL);
    void destroy();

    void beginFrame();
    void prepare(const BasicShadowFramePayload& payload);
    void dispatchCull(ICommandBuffer* cmdBuf, uint32_t flightIndex) const;
    void renderFace(ICommandBuffer* cmdBuf,
                    const BasicShadowFramePayload& payload,
                    const PointShadowFacePayload& facePayload) const;
    void refreshPipeline(EFormat::T depthFormat);

    [[nodiscard]] bool isSupported() const { return _bSupported; }
    [[nodiscard]] bool hasRenderableInstances(uint32_t flightIndex) const;

  private:
    struct MeshBatch
    {
        Mesh*    mesh          = nullptr;
        uint32_t firstInstance = 0;
        uint32_t instanceCount = 0;
    };

    enum class ECullPlugin
    {
        Disabled,
        NoCull,
        Compute,
    };

    struct PerFlightResources
    {
        stdptr<IBuffer>     instanceBuffer;
        stdptr<IBuffer>     noCullDrawCommandBuffer;
        stdptr<IBuffer>     noCullDrawCountBuffer;
        DescriptorSetHandle indirectDS = nullptr;

        ECullPlugin activeCullPlugin = ECullPlugin::Disabled;
        std::vector<MeshBatch> meshBatches;
        uint32_t               totalInstances = 0;
    };

    struct ShadowPipelineVariant
    {
        stdptr<IGraphicsPipeline> pipeline;
        stdptr<IPipelineLayout>   pipelineLayout;
    };

    void ensureInstanceCapacity(uint32_t requiredCount);
    void ensureNoCullCommandCapacity(uint32_t faceCount);
    void uploadNoCullCommands(uint32_t flightIndex, uint32_t faceCount, const std::vector<PointShadowInstanceData>& instanceData);
    void updateIndirectDescriptors(uint32_t flightIndex);

    IRender* _render = nullptr;

    bool _bSupported = false;

    stdptr<IDescriptorSetLayout> _frameDSL;
    stdptr<IDescriptorSetLayout> _indirectDSL;
    stdptr<IDescriptorPool>      _dsp;

    ShadowPipelineVariant      _staticVariant;
    GraphicsPipelineCreateInfo _pipelineCI{};

    PointShadowCullPass _cullPass;

    std::array<PerFlightResources, MAX_FLIGHTS_IN_FLIGHT> _perFlight{};
    uint32_t _instanceCapacity       = 0;
    uint32_t _noCullFaceCapacity     = 0;

    std::unordered_map<Mesh*, uint32_t> _meshBatchMap;
};

} // namespace ya
