#pragma once

#include "RHI/Core/Buffer.h"
#include "RHI/Core/CommandBuffer.h"
#include "RHI/Core/DescriptorSet.h"
#include "RHI/Core/Pipeline.h"
#include "EntityId.slang.h"

#include <glm/glm.hpp>
#include <memory>

namespace ya
{

struct IRender;
struct RenderDrawItem;
struct RenderFrameData;

/// Camera-facing billboard quad written into the entity-id target, mirroring
/// the world-size math of the billboard overlay pass so the id under the
/// cursor matches what is actually visible.
struct EntityIdBillboard
{
    glm::vec3 worldCenter = glm::vec3(0.0f);
    glm::vec2 worldSize   = glm::vec2(1.0f);
    uint32_t  entityId    = 0;
};

/// Renders every draw item into an R32_UINT target using its entity id as the
/// pixel value. Depth-testing against the viewport depth keeps the ids aligned
/// with what is actually visible, so a readback at a screen position yields the
/// exact entity under the cursor.
struct EntityIdViewportPass
{
    using FrameUBO      = slang_types::EntityId::FrameData;
    using PushConstants = slang_types::EntityId::PushConstants;

    void init(IRender* render, EFormat::T colorFormat, EFormat::T depthFormat);
    void destroy();

    void execute(ICommandBuffer*  cmdBuf,
                 uint32_t         viewportWidth,
                 uint32_t         viewportHeight,
                 const glm::mat4& viewProj,
                 const glm::mat4& view,
                 const RenderFrameData& frameData,
                 DescriptorSetHandle    skinningDescriptorSet,
                 const std::vector<EntityIdBillboard>& billboards = {});

  private:
    void drawStaticBucket(ICommandBuffer* cmdBuf, const std::vector<RenderDrawItem>& items);
    void drawSkinnedBucket(ICommandBuffer* cmdBuf, const std::vector<RenderDrawItem>& items);
    void drawBillboards(ICommandBuffer* cmdBuf, const std::vector<EntityIdBillboard>& billboards);

    IRender* _render = nullptr;

    std::shared_ptr<IDescriptorPool>       _descriptorPool;
    std::shared_ptr<IDescriptorSetLayout>  _frameDSL;
    std::shared_ptr<IDescriptorSetLayout>  _skinningDSL;
    DescriptorSetHandle                    _frameDS = nullptr;
    std::shared_ptr<IBuffer>               _frameUBO;
    std::shared_ptr<IPipelineLayout>       _pipelineLayout;
    std::shared_ptr<IGraphicsPipeline>     _pipeline;
    std::shared_ptr<IPipelineLayout>       _billboardPipelineLayout;
    std::shared_ptr<IGraphicsPipeline>     _billboardPipeline;
    std::shared_ptr<IPipelineLayout>       _skinnedPipelineLayout;
    std::shared_ptr<IGraphicsPipeline>     _skinnedPipeline;
};

} // namespace ya
