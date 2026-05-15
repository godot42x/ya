#pragma once

#include "Render/Core/DescriptorSet.h"
#include "Render/Shadow/ShadowSettings.h"

#include "CombineShadowMappingGenerate.slang.h"
#include "Shadow.PointShadowIndirect.slang.h"

#include <cstdint>

namespace ya
{

struct RenderFrameData;
struct Texture;

struct BasicShadowFramePayload
{
    using FrameUBO     = slang_types::CombineShadowMappingGenerate::FrameData;
    using PointFaceUBO = slang_types::Shadow::PointShadowIndirect::PointLightFaceData;

    uint32_t               flightIndex = 0;
    const RenderFrameData* frameData   = nullptr;
    const ShadowSettings*  settings    = nullptr;

    FrameUBO frameUBO{};
    uint32_t pointLightCount       = 0;
    bool     directionalEnabled    = false;
    bool     pointEnabled              = false;
    bool     pointIndirectRequested    = false;
    bool     pointIndirectCullEnabled  = false;
};

struct PointShadowFacePayload
{
    uint32_t lightIndex       = 0;
    uint32_t faceIndex        = 0;
    uint32_t faceGlobalIndex  = 0;
    uint32_t layerIndex       = 0;

    DescriptorSetHandle faceDS = nullptr;
    Texture*            depthTexture = nullptr;
};

} // namespace ya
