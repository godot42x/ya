#include "ShadowDrawHelper.h"

#include "Core/Profiling/PerfKeys.h"
#include "Core/Profiling/PerfState.h"

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
    YA_PERF_SCOPE(perf::sample::shadowPointDirectDrawStatic(), perf::metric::cpuTimeMs(), perf::domain::render());

    auto drawItems = [&](DrawCandidateView items)
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

    drawItems(DrawCandidateView{std::span<const RenderDrawItem>(buckets.pbrDrawItems)});
    drawItems(DrawCandidateView{std::span<const RenderDrawItem>(buckets.phongDrawItems)});
    drawItems(DrawCandidateView{std::span<const RenderDrawItem>(buckets.unlitDrawItems)});
    drawItems(DrawCandidateView{std::span<const RenderDrawItem>(buckets.simpleDrawItems)});
    drawItems(DrawCandidateView{std::span<const RenderDrawItem>(buckets.fallbackDrawItems)});
}

void drawSkinnedBuckets(ICommandBuffer* cmdBuf,
                        const PassResources& res,
                        const RenderShadingDrawBuckets& buckets)
{
    auto drawItems = [&](DrawCandidateView items)
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

    drawItems(DrawCandidateView{std::span<const RenderDrawItem>(buckets.pbrDrawItems)});
    drawItems(DrawCandidateView{std::span<const RenderDrawItem>(buckets.phongDrawItems)});
    drawItems(DrawCandidateView{std::span<const RenderDrawItem>(buckets.unlitDrawItems)});
    drawItems(DrawCandidateView{std::span<const RenderDrawItem>(buckets.simpleDrawItems)});
    drawItems(DrawCandidateView{std::span<const RenderDrawItem>(buckets.fallbackDrawItems)});
}

} // namespace ya::ShadowDrawHelper
