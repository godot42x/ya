#pragma once

#include "ImGui.h"
#include "Render/Core/DescriptorSet.h"
#include "Render/Core/Image.h"

namespace ya
{

struct ImGuiImageEntry
{
    stdptr<IImageView>  imageView;
    stdptr<Sampler>     sampler;
    DescriptorSetHandle ds;

    bool operator==(const ImGuiImageEntry &other) const
    {
        return imageView == other.imageView && sampler == other.sampler;
    }
    bool operator<(const ImGuiImageEntry &other) const
    {
        if (imageView != other.imageView)
            return imageView < other.imageView;
        return sampler < other.sampler;
    }
    operator bool() const
    {
        return imageView != nullptr && sampler != nullptr && ds != nullptr;
    }

    bool isValid() const
    {
        return this->operator bool();
    }

    operator ImTextureRef() const
    {
        return (ImTextureRef)ds.ptr;
    }
};

} // namespace ya

// Provide std::hash specialization for unordered_set support
namespace std
{
template <>
struct hash<ya::ImGuiImageEntry>
{
    size_t operator()(const ya::ImGuiImageEntry &entry) const noexcept
    {
        size_t h1 = hash<void *>{}(entry.sampler.get());
        size_t h2 = hash<void *>{}(entry.imageView.get());
        return h1 ^ (h2 << 1);
    }
};
} // namespace std
