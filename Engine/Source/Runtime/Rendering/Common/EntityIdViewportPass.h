#pragma once

#include "Render/Core/Buffer.h"
#include "Render/Core/CommandBuffer.h"
#include "Render/Core/DescriptorSet.h"
#include "Render/Core/Pipeline.h"

#include <glm/glm.hpp>
#include <memory>

namespace ya
{

struct IRender;
struct RenderDrawItem;
struct RenderFrameData;

/// Renders every draw item into an R32_UINT target using its entity id as the
/// pixel value. Depth-testing against the viewport depth keeps the ids aligned
/// with what is actually visible, so a readback at a screen position yields the
/// exact entity under the cursor.
struct EntityIdViewportPass
{
    struct FrameUBO
    {
        glm::mat4 viewProj = glm::mat4(1.0f);
        glm::mat4 view     = glm::mat4(1.0f);
    };

    struct PushConstants
    {
        glm::mat4 modelMat           = glm::mat4(1.0f);
        uint32_t  entityId            = 0;
        int32_t  skinningPaletteIndex = -1;
    };

    void init(IRender* render, EFormat::T colorFormat, EFormat::T depthFormat);
    void destroy();

    void execute(ICommandBuffer*  cmdBuf,
                 uint32_t         viewportWidth,
                 uint32_t         viewportHeight,
                 const glm::mat4& viewProj,
                 const glm::mat4& view,
                 const RenderFrameData& frameData,
                 DescriptorSetHandle    skinningDescriptorSet);

  private:
    void drawStaticBucket(ICommandBuffer* cmdBuf, const std::vector<RenderDrawItem>& items);
    void drawSkinnedBucket(ICommandBuffer* cmdBuf, const std::vector<RenderDrawItem>& items);

    IRender* _render = nullptr;

    std::shared_ptr<IDescriptorPool>       _descriptorPool;
    std::shared_ptr<IDescriptorSetLayout>  _frameDSL;
    std::shared_ptr<IDescriptorSetLayout>  _skinningDSL;
    DescriptorSetHandle                    _frameDS = nullptr;
    std::shared_ptr<IBuffer>               _frameUBO;
    std::shared_ptr<IPipelineLayout>       _pipelineLayout;
    std::shared_ptr<IGraphicsPipeline>     _pipeline;
    std::shared_ptr<IPipelineLayout>       _skinnedPipelineLayout;
    std::shared_ptr<IGraphicsPipeline>     _skinnedPipeline;
};

} // namespace ya
