#pragma once

#include "Render/RenderDefines.h"

#include <unordered_map>
#include <vector>

namespace ya
{

struct IImage;

struct ImageLayoutTransition
{
    IImage*             image = nullptr;
    ImageResourceState  oldState{};
    ImageResourceState  newState{};
    ImageSubresourceRange range{};
};

struct ImageLayoutExpectationMismatch
{
    IImage*               image           = nullptr;
    EImageLayout::T       expectedLayout  = EImageLayout::Undefined;
    EImageLayout::T       actualLayout    = EImageLayout::Undefined;
    ImageSubresourceRange range{};
};

class ResourceStateTracker
{
  private:
    struct ImageSubresourceKey
    {
        const IImage* image  = nullptr;
        uint32_t      aspect = 0;
        uint32_t      mip    = 0;
        uint32_t      layer  = 0;

        bool operator==(const ImageSubresourceKey&) const = default;
    };

    struct ImageSubresourceKeyHash
    {
        size_t operator()(const ImageSubresourceKey& key) const;
    };

    std::unordered_map<ImageSubresourceKey, ImageResourceState, ImageSubresourceKeyHash> _imageStates;

    static ImageSubresourceRange normalizeRange(const IImage& image, const ImageSubresourceRange* range);
    void seedImage(const IImage& image);
    ImageResourceState getState(const IImage& image, uint32_t aspect, uint32_t mip, uint32_t layer) const;
    static ImageResourceState makeSeedState(const IImage& image, uint32_t aspect, uint32_t mip, uint32_t layer);

  public:
    void reset();
    void setState(IImage& image, const ImageResourceState& state, const ImageSubresourceRange* range = nullptr);
    void setLayout(IImage& image, EImageLayout::T layout, const ImageSubresourceRange* range = nullptr);
    [[nodiscard]] std::vector<ImageLayoutExpectationMismatch> validateLayout(
        IImage& image,
        EImageLayout::T expectedLayout,
        const ImageSubresourceRange* range = nullptr);
    [[nodiscard]] std::vector<ImageLayoutTransition> transition(
        IImage& image,
        const ImageResourceState& newState,
        const ImageSubresourceRange* range = nullptr);
    [[nodiscard]] std::vector<ImageLayoutTransition> transition(
        IImage& image,
        EImageLayout::T newLayout,
        const ImageSubresourceRange* range = nullptr);
};

} // namespace ya
