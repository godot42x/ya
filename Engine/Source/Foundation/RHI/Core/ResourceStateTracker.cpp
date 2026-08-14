#include "ResourceStateTracker.h"

#include "RHI/Core/Image.h"

#include <functional>

namespace ya
{

namespace
{

uint32_t defaultAspectMask(EFormat::T format)
{
    if (EFormat::isDepthStencilFormat(format)) {
        return EImageAspect::DepthStencil;
    }
    if (EFormat::isDepthFormat(format)) {
        return EImageAspect::Depth;
    }
    return EImageAspect::Color;
}

template <typename Fn>
void forEachAspect(uint32_t aspectMask, Fn&& fn)
{
    for (uint32_t bit = 1; bit != 0; bit <<= 1) {
        if ((aspectMask & bit) != 0) {
            fn(bit);
        }
    }
}

} // namespace

ImageSubresourceRange ResourceStateTracker::normalizeRange(const IImage& image, const ImageSubresourceRange* range)
{
    ImageSubresourceRange normalized = range ? *range : ImageSubresourceRange{
        .aspectMask     = defaultAspectMask(image.getFormat()),
        .baseMipLevel   = 0,
        .levelCount     = image.getMipLevels(),
        .baseArrayLayer = 0,
        .layerCount     = image.getArrayLayers(),
    };

    YA_CORE_ASSERT(normalized.aspectMask != 0, "Image transition aspect mask must not be empty");
    YA_CORE_ASSERT(normalized.levelCount > 0 && normalized.baseMipLevel + normalized.levelCount <= image.getMipLevels(),
                   "Image transition mip range [{}, {}) exceeds {} mip levels",
                   normalized.baseMipLevel, normalized.baseMipLevel + normalized.levelCount, image.getMipLevels());
    YA_CORE_ASSERT(normalized.layerCount > 0 && normalized.baseArrayLayer + normalized.layerCount <= image.getArrayLayers(),
                   "Image transition layer range [{}, {}) exceeds {} array layers",
                   normalized.baseArrayLayer, normalized.baseArrayLayer + normalized.layerCount, image.getArrayLayers());
    return normalized;
}

ImageResourceState ResourceStateTracker::makeSeedState(const IImage& image, uint32_t aspect, uint32_t mip, uint32_t layer)
{
    ImageResourceState state;
    state.layout = image.getCompatibilityLayout(aspect, mip, layer);
    state.subresourceRange = ImageSubresourceRange{
        .aspectMask     = aspect,
        .baseMipLevel   = mip,
        .levelCount     = 1,
        .baseArrayLayer = layer,
        .layerCount     = 1,
    };
    return state;
}

ImageResourceState ResourceStateTracker::getState(const IImage& image, uint32_t aspect, uint32_t mip, uint32_t layer) const
{
    const auto it = _imageStates.find(ImageSubresourceKey{.image = &image, .aspect = aspect, .mip = mip, .layer = layer});
    return it != _imageStates.end() ? it->second : makeSeedState(image, aspect, mip, layer);
}

void ResourceStateTracker::seedImage(const IImage& image)
{
    const auto probe = ImageSubresourceKey{
        .image  = &image,
        .aspect = defaultAspectMask(image.getFormat()) & (~defaultAspectMask(image.getFormat()) + 1u),
        .mip    = 0,
        .layer  = 0,
    };
    if (_imageStates.contains(probe)) {
        return;
    }
    forEachAspect(defaultAspectMask(image.getFormat()), [&](uint32_t aspect) {
        for (uint32_t mip = 0; mip < image.getMipLevels(); ++mip) {
            for (uint32_t layer = 0; layer < image.getArrayLayers(); ++layer) {
                _imageStates[ImageSubresourceKey{.image = &image, .aspect = aspect, .mip = mip, .layer = layer}] =
                    makeSeedState(image, aspect, mip, layer);
            }
        }
    });
}

void ResourceStateTracker::reset()
{
    _imageStates.clear();
}

void ResourceStateTracker::setState(IImage& image, const ImageResourceState& state, const ImageSubresourceRange* range)
{
    seedImage(image);
    const auto normalized = normalizeRange(image, range);
    forEachAspect(normalized.aspectMask, [&](uint32_t aspect) {
        for (uint32_t mip = normalized.baseMipLevel; mip < normalized.baseMipLevel + normalized.levelCount; ++mip) {
            for (uint32_t layer = normalized.baseArrayLayer; layer < normalized.baseArrayLayer + normalized.layerCount; ++layer) {
                auto trackedState = state;
                trackedState.subresourceRange = ImageSubresourceRange{
                    .aspectMask     = aspect,
                    .baseMipLevel   = mip,
                    .levelCount     = 1,
                    .baseArrayLayer = layer,
                    .layerCount     = 1,
                };
                _imageStates[ImageSubresourceKey{.image = &image, .aspect = aspect, .mip = mip, .layer = layer}] = trackedState;
            }
        }
    });
}

void ResourceStateTracker::setLayout(IImage& image, EImageLayout::T layout, const ImageSubresourceRange* range)
{
    ImageResourceState state;
    state.layout = layout;
    setState(image, state, range);
}

std::vector<ImageLayoutExpectationMismatch> ResourceStateTracker::validateLayout(
    IImage& image,
    EImageLayout::T expectedLayout,
    const ImageSubresourceRange* range)
{
    seedImage(image);
    const auto normalized = normalizeRange(image, range);
    std::vector<ImageLayoutExpectationMismatch> mismatches;

    forEachAspect(normalized.aspectMask, [&](uint32_t aspect) {
        for (uint32_t mip = normalized.baseMipLevel; mip < normalized.baseMipLevel + normalized.levelCount; ++mip) {
            uint32_t layer = normalized.baseArrayLayer;
            while (layer < normalized.baseArrayLayer + normalized.layerCount) {
                const auto actualLayout = getState(image, aspect, mip, layer).layout;
                const auto runStart     = layer;
                while (layer < normalized.baseArrayLayer + normalized.layerCount &&
                       getState(image, aspect, mip, layer).layout == actualLayout) {
                    ++layer;
                }

                if (actualLayout != expectedLayout) {
                    mismatches.push_back({
                        .image          = &image,
                        .expectedLayout = expectedLayout,
                        .actualLayout   = actualLayout,
                        .range = ImageSubresourceRange{
                            .aspectMask     = aspect,
                            .baseMipLevel   = mip,
                            .levelCount     = 1,
                            .baseArrayLayer = runStart,
                            .layerCount     = layer - runStart,
                        },
                    });
                }
            }
        }
    });

    return mismatches;
}

std::vector<ImageLayoutTransition> ResourceStateTracker::transition(
    IImage& image,
    const ImageResourceState& newState,
    const ImageSubresourceRange* range)
{
    seedImage(image);
    const auto normalized = normalizeRange(image, range);
    std::vector<ImageLayoutTransition> transitions;
    auto newStateTemplate             = newState;
    newStateTemplate.subresourceRange = normalized;

    EImageLayout::T commonOldLayout  = EImageLayout::Undefined;
    bool            bHasCommonLayout = true;
    bool            bFirst           = true;
    forEachAspect(normalized.aspectMask, [&](uint32_t aspect) {
        for (uint32_t mip = normalized.baseMipLevel; mip < normalized.baseMipLevel + normalized.levelCount; ++mip) {
            for (uint32_t layer = normalized.baseArrayLayer; layer < normalized.baseArrayLayer + normalized.layerCount; ++layer) {
                const auto layout = getState(image, aspect, mip, layer).layout;
                if (bFirst) {
                    commonOldLayout = layout;
                    bFirst          = false;
                } else if (layout != commonOldLayout) {
                    bHasCommonLayout = false;
                }
            }
        }
    });

    if (bHasCommonLayout) {
        if (commonOldLayout != newState.layout) {
            auto oldState = getState(
                image,
                normalized.aspectMask & (~normalized.aspectMask + 1u),
                normalized.baseMipLevel,
                normalized.baseArrayLayer);
            auto mergedState             = newStateTemplate;
            mergedState.subresourceRange = normalized;
            transitions.push_back({.image = &image, .oldState = oldState, .newState = mergedState, .range = normalized});
        }
        setState(image, newStateTemplate, &normalized);
        return transitions;
    }

    forEachAspect(normalized.aspectMask, [&](uint32_t aspect) {
        for (uint32_t mip = normalized.baseMipLevel; mip < normalized.baseMipLevel + normalized.levelCount; ++mip) {
            uint32_t layer = normalized.baseArrayLayer;
            while (layer < normalized.baseArrayLayer + normalized.layerCount) {
                const auto oldState = getState(image, aspect, mip, layer);
                const auto runStart = layer;
                while (layer < normalized.baseArrayLayer + normalized.layerCount &&
                       getState(image, aspect, mip, layer).layout == oldState.layout) {
                    ++layer;
                }
                if (oldState.layout != newState.layout) {
                    const auto transitionRange = ImageSubresourceRange{
                        .aspectMask     = aspect,
                        .baseMipLevel   = mip,
                        .levelCount     = 1,
                        .baseArrayLayer = runStart,
                        .layerCount     = layer - runStart,
                    };
                    auto mergedState             = newStateTemplate;
                    mergedState.subresourceRange = transitionRange;
                    transitions.push_back({
                        .image    = &image,
                        .oldState = oldState,
                        .newState = mergedState,
                        .range    = transitionRange,
                    });
                }
            }
        }
    });

    setState(image, newStateTemplate, &normalized);
    return transitions;
}

std::vector<ImageLayoutTransition> ResourceStateTracker::transition(
    IImage& image,
    EImageLayout::T newLayout,
    const ImageSubresourceRange* range)
{
    ImageResourceState state;
    state.layout = newLayout;
    return transition(image, state, range);
}

} // namespace ya
