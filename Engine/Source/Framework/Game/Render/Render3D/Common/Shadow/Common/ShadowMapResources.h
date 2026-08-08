#pragma once

#include "RHI/Core/Image.h"
#include "RHI/Render.h"
#include "RHI/Core/Sampler.h"
#include "RHI/Render.h"

#include <array>
#include <string_view>

namespace ya
{

constexpr uint32_t SHADOW_DIRECTIONAL_LAYER_INDEX      = 0;
constexpr uint32_t SHADOW_RESERVED_DIRECTIONAL_LAYERS  = 6;
constexpr uint32_t SHADOW_POINT_LIGHT_LAYER_STRIDE     = 6;
constexpr uint32_t SHADOW_POINT_LIGHT_BASE_LAYER_INDEX = SHADOW_RESERVED_DIRECTIONAL_LAYERS;

constexpr uint32_t getShadowPointLightBaseLayer(uint32_t lightIndex)
{
    return SHADOW_POINT_LIGHT_BASE_LAYER_INDEX + lightIndex * SHADOW_POINT_LIGHT_LAYER_STRIDE;
}

constexpr uint32_t getShadowTotalLayerCount()
{
    return SHADOW_RESERVED_DIRECTIONAL_LAYERS + MAX_POINT_LIGHTS * SHADOW_POINT_LIGHT_LAYER_STRIDE;
}

struct ShadowMapResourceDesc
{
    std::string_view imageLabel;
    std::string_view samplerLabel;
    std::string_view viewLabelPrefix;
    Extent2D         extent{};
    EFormat::T       depthFormat = EFormat::D32_SFLOAT;
};

struct ShadowMapResources
{
    stdptr<Sampler>                                                 sampler;
    stdptr<IImage>                                                  depthImage;
    Extent2D                                                        extent{};
    EFormat::T                                                      depthFormat = EFormat::Undefined;
    uint32_t                                                        layerCount = 0;
    stdptr<IImageView>                                              directionalDepthIV;
    std::array<stdptr<IImageView>, MAX_POINT_LIGHTS>                pointCubeIVs{};
    std::array<std::array<stdptr<IImageView>, 6>, MAX_POINT_LIGHTS> pointFaceIVs{};

    void init(IRender* render, const ShadowMapResourceDesc& desc);
    void destroy();
    void rebuildViews(IRender* render, std::string_view viewLabelPrefix);
};

} // namespace ya
