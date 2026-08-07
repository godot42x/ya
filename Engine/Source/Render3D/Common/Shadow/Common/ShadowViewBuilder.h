#pragma once

#include "RHI/Core/RenderResourceFactory.h"

#include <array>
#include <memory>
#include <string_view>

namespace ya::ShadowViewBuilder
{

struct LayerViews
{
    stdptr<IImageView>                                              directionalDepthIV;
    std::array<stdptr<IImageView>, MAX_POINT_LIGHTS>                pointCubeIVs{};
    std::array<std::array<stdptr<IImageView>, 6>, MAX_POINT_LIGHTS> pointFaceIVs{};
};

[[nodiscard]] LayerViews buildLayerViews(IRenderResourceFactory* resourceFactory,
                                         const std::shared_ptr<IImage>& shadowImage,
                                         std::string_view               labelPrefix);

} // namespace ya::ShadowViewBuilder
