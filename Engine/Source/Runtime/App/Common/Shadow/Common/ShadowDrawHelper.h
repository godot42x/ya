#pragma once

#include "Render/Core/DescriptorSet.h"

#include "CombineShadowMappingGenerate.slang.h"

namespace ya
{

struct ICommandBuffer;
struct IGraphicsPipeline;
struct IPipelineLayout;
struct RenderShadingDrawBuckets;

// ═══════════════════════════════════════════════════════════════════════════
// ShadowDrawHelper
//
// Stateless draw helpers shared by DirectionalShadowPass and PointShadowPass.
// Both passes feed shadow geometry through the same shader
// (CombineShadowMappingGenerate.slang) with identical descriptor layout
// (set 0 = frame/face UBO, set 1 = skinning SSBO) and the same push-constant
// struct, so the per-bucket loop can be factored out.
// ═══════════════════════════════════════════════════════════════════════════
namespace ShadowDrawHelper
{
    using ModelPushConstant = slang_types::CombineShadowMappingGenerate::PushConstants;

    struct PassResources
    {
        IGraphicsPipeline*  pipeline       = nullptr;
        IPipelineLayout*    pipelineLayout = nullptr;
        DescriptorSetHandle frameDS        = nullptr; // bound to set 0
        DescriptorSetHandle skinningDS     = nullptr; // bound to set 1 (skinned only)
    };

    /// Draw all static-mesh buckets with the given pipeline / frame DS.
    void drawStaticBuckets(ICommandBuffer* cmdBuf,
                           const PassResources& res,
                           const RenderShadingDrawBuckets& buckets);

    /// Draw all skinned-mesh buckets. Requires res.skinningDS != nullptr.
    void drawSkinnedBuckets(ICommandBuffer* cmdBuf,
                            const PassResources& res,
                            const RenderShadingDrawBuckets& buckets);
}

} // namespace ya
