#include "ShadowViewBuilder.h"

#include <format>

namespace ya::ShadowViewBuilder
{

LayerViews buildLayerViews(ITextureFactory* textureFactory,
                           const std::shared_ptr<IImage>& shadowImage,
                           std::string_view               labelPrefix)
{
    YA_CORE_ASSERT(textureFactory, "Shadow view builder requires a texture factory");
    YA_CORE_ASSERT(shadowImage, "Shadow view builder requires a shadow image");

    LayerViews views{};
    const std::string prefix(labelPrefix);

    views.directionalDepthIV = textureFactory->createImageView(
        shadowImage,
        ImageViewCreateInfo{
            .label          = std::format("{} Directional Depth IV", prefix),
            .viewType       = EImageViewType::View2D,
            .aspectFlags    = EImageAspect::Depth,
            .baseMipLevel   = 0,
            .levelCount     = 1,
            .baseArrayLayer = 0,
            .layerCount     = 1,
        });

    for (uint32_t lightIndex = 0; lightIndex < MAX_POINT_LIGHTS; ++lightIndex) {
        views.pointCubeIVs[lightIndex] = textureFactory->createImageView(
            shadowImage,
            ImageViewCreateInfo{
                .label          = std::format("{} Point[{}] CubeIV", prefix, lightIndex),
                .viewType       = EImageViewType::ViewCube,
                .aspectFlags    = EImageAspect::Depth,
                .baseMipLevel   = 0,
                .levelCount     = 1,
                .baseArrayLayer = 1 + lightIndex * 6,
                .layerCount     = 6,
            });

        for (uint32_t faceIndex = 0; faceIndex < 6; ++faceIndex) {
            views.pointFaceIVs[lightIndex][faceIndex] = textureFactory->createImageView(
                shadowImage,
                ImageViewCreateInfo{
                    .label          = std::format("{} Point[{}] Face[{}]", prefix, lightIndex, faceIndex),
                    .viewType       = EImageViewType::View2D,
                    .aspectFlags    = EImageAspect::Depth,
                    .baseMipLevel   = 0,
                    .levelCount     = 1,
                    .baseArrayLayer = 1 + lightIndex * 6 + faceIndex,
                    .layerCount     = 1,
                });
        }
    }

    return views;
}

} // namespace ya::ShadowViewBuilder
