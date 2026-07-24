#include "ShadowViewBuilder.h"
#include "ShadowMapResources.h"

#include <format>

namespace ya::ShadowViewBuilder
{

LayerViews buildLayerViews(IRenderResourceFactory* resourceFactory,
                           const std::shared_ptr<IImage>& shadowImage,
                           std::string_view               labelPrefix)
{
    YA_CORE_ASSERT(resourceFactory, "Shadow view builder requires a resource factory");
    YA_CORE_ASSERT(shadowImage, "Shadow view builder requires a shadow image");

    LayerViews views{};
    const std::string prefix(labelPrefix);

    views.directionalDepthIV = resourceFactory->createImageView(
        shadowImage,
        ImageViewCreateInfo{
            .label          = std::format("{} Directional Depth IV", prefix),
            .viewType       = EImageViewType::View2DArray,
            .aspectFlags    = EImageAspect::Depth,
            .baseMipLevel   = 0,
            .levelCount     = 1,
            .baseArrayLayer = SHADOW_DIRECTIONAL_LAYER_INDEX,
            .layerCount     = MAX_DIRECTIONAL_CASCADES,
        });

    for (uint32_t lightIndex = 0; lightIndex < MAX_POINT_LIGHTS; ++lightIndex) {
        const uint32_t pointBaseLayer = getShadowPointLightBaseLayer(lightIndex);
        views.pointCubeIVs[lightIndex] = resourceFactory->createImageView(
            shadowImage,
            ImageViewCreateInfo{
                .label          = std::format("{} Point[{}] CubeIV", prefix, lightIndex),
                .viewType       = EImageViewType::ViewCube,
                .aspectFlags    = EImageAspect::Depth,
                .baseMipLevel   = 0,
                .levelCount     = 1,
                .baseArrayLayer = pointBaseLayer,
                .layerCount     = SHADOW_POINT_LIGHT_LAYER_STRIDE,
            });

        for (uint32_t faceIndex = 0; faceIndex < 6; ++faceIndex) {
            views.pointFaceIVs[lightIndex][faceIndex] = resourceFactory->createImageView(
                shadowImage,
                ImageViewCreateInfo{
                    .label          = std::format("{} Point[{}] Face[{}]", prefix, lightIndex, faceIndex),
                    .viewType       = EImageViewType::View2D,
                    .aspectFlags    = EImageAspect::Depth,
                    .baseMipLevel   = 0,
                    .levelCount     = 1,
                    .baseArrayLayer = pointBaseLayer + faceIndex,
                    .layerCount     = 1,
                });
        }
    }

    return views;
}

} // namespace ya::ShadowViewBuilder
