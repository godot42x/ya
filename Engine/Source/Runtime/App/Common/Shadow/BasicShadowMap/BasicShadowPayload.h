#pragma once

#include "Render/Core/DescriptorSet.h"
#include "Render/RenderFrameData.h"
#include "Render/Shadow/ShadowSettings.h"

#include "CombineShadowMappingGenerate.slang.h"
#include "Shadow.PointShadowIndirect.slang.h"

#include <cstdint>

namespace ya
{

struct RenderFrameData;
struct IImage;
struct IImageView;

struct BasicShadowFramePayload
{
    using FrameUBO     = slang_types::CombineShadowMappingGenerate::FrameData;
    using PointFaceUBO = slang_types::Shadow::PointShadowIndirect::PointShadowFaceData;

    uint32_t               flightIndex = 0;
    const RenderFrameData* frameData   = nullptr;
    const ShadowSettings*  settings    = nullptr;

    FrameUBO frameUBO{};
    uint32_t pointLightCount = 0;

    // ─── Derived flags (read-only; computed from settings + frame state) ─
    [[nodiscard]] bool directionalEnabled() const
    {
        return settings && settings->directionalEnabled
            && frameData && frameData->bHasDirectionalLight;
    }
    [[nodiscard]] uint32_t directionalCascadeCount() const
    {
        if (!directionalEnabled()) return 0;
        return std::clamp(frameData->directionalLight.cascadeCount,
                          1u,
                          static_cast<uint32_t>(MAX_DIRECTIONAL_CASCADES));
    }
    [[nodiscard]] bool pointEnabled() const
    {
        return settings && settings->pointLightEnabled && pointLightCount > 0;
    }
    [[nodiscard]] bool pointIndirectRequested() const
    {
        return settings && settings->pointLightUseIndirect;
    }
    [[nodiscard]] bool pointIndirectCullEnabled() const
    {
        return settings && settings->pointLightIndirectCullEnabled;
    }
};

struct PointShadowFacePayload
{
    uint32_t lightIndex       = 0;
    uint32_t faceIndex        = 0;
    uint32_t faceGlobalIndex  = 0;
    uint32_t layerIndex       = 0;

    DescriptorSetHandle faceDS     = nullptr;
    IImage*             depthImage = nullptr;
    IImageView*         depthView  = nullptr;
};

} // namespace ya
