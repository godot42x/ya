#include "ShadowDrawHelper.h"

#include "Render/Core/CommandBuffer.h"
#include "Render/Core/Pipeline.h"
#include "Render/Mesh.h"
#include "Render/RenderFrameData.h"

namespace ya::ShadowDrawHelper
{

void drawStaticBuckets(ICommandBuffer* cmdBuf,
                       const PassResources& res,
                       const RenderShadingDrawBuckets& buckets)
{
    auto drawItems = [&](const std::vector<RenderDrawItem>& items)
    {
        if (items.empty()) return;
        cmdBuf->bindPipeline(res.pipeline);
        cmdBuf->bindDescriptorSets(res.pipelineLayout, 0, {res.frameDS});
        for (const auto& item : items) {
            if (!item.mesh) continue;
            ModelPushConstant pc{.modelMat = item.worldMatrix, .skinningPaletteIndex = -1};
            cmdBuf->pushConstants(res.pipelineLayout, EShaderStage::Vertex, 0, sizeof(ModelPushConstant), &pc);
            item.mesh->drawStatic(cmdBuf);
        }
    };

    drawItems(buckets.pbrDrawItems);
    drawItems(buckets.phongDrawItems);
    drawItems(buckets.unlitDrawItems);
    drawItems(buckets.simpleDrawItems);
    drawItems(buckets.fallbackDrawItems);
}

void drawSkinnedBuckets(ICommandBuffer* cmdBuf,
                        const PassResources& res,
                        const RenderShadingDrawBuckets& buckets)
{
    auto drawItems = [&](const std::vector<RenderDrawItem>& items)
    {
        if (items.empty()) return;
        cmdBuf->bindPipeline(res.pipeline);
        cmdBuf->bindDescriptorSets(res.pipelineLayout, 0, {res.frameDS, res.skinningDS});
        for (const auto& item : items) {
            if (!item.mesh) continue;
            ModelPushConstant pc{.modelMat = item.worldMatrix, .skinningPaletteIndex = item.skinningPaletteIndex};
            cmdBuf->pushConstants(res.pipelineLayout, EShaderStage::Vertex, 0, sizeof(ModelPushConstant), &pc);
            item.mesh->drawSkinned(cmdBuf);
        }
    };

    drawItems(buckets.pbrDrawItems);
    drawItems(buckets.phongDrawItems);
    drawItems(buckets.unlitDrawItems);
    drawItems(buckets.simpleDrawItems);
    drawItems(buckets.fallbackDrawItems);
}

} // namespace ya::ShadowDrawHelper
