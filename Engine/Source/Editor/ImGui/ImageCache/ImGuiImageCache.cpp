#include "Editor/ImGui/ImGuiHelper.h"

#include "Platform/Render/Vulkan/VulkanUtils.h"
#include "Render/Core/Image.h"
#include "Render/Core/Sampler.h"

#include <unordered_map>
#include <vector>

namespace ya
{

namespace
{

constexpr uint64_t IMGUI_DESCRIPTOR_GC_DELAY_FRAMES = 8;

struct ImageCacheKey
{
    IImageView* imageView = nullptr;
    Sampler*    sampler   = nullptr;

    bool operator==(const ImageCacheKey& other) const
    {
        return imageView == other.imageView && sampler == other.sampler;
    }
};

struct ImageCacheKeyHash
{
    size_t operator()(const ImageCacheKey& key) const noexcept
    {
        size_t h1 = std::hash<void*>{}(key.imageView);
        size_t h2 = std::hash<void*>{}(key.sampler);
        return h1 ^ (h2 << 1);
    }
};

struct ImageCacheEntry
{
    ImageViewHandle handle        = {};
    void*           ds            = nullptr;
    uint64_t        lastUsedFrame = 0;
};

struct RetiredDescriptorSet
{
    void*    ds          = nullptr;
    uint64_t retireFrame = 0;
};

std::unordered_map<ImageCacheKey, ImageCacheEntry, ImageCacheKeyHash> g_imageCache;
std::vector<RetiredDescriptorSet>                                     g_retiredDescriptorSets;
uint64_t                                                              g_imguiFrameIndex = 0;

void retireDescriptorSet(void* descriptorSet)
{
    if (!descriptorSet) {
        return;
    }
    g_retiredDescriptorSets.push_back(RetiredDescriptorSet{
        .ds          = descriptorSet,
        .retireFrame = g_imguiFrameIndex,
    });
}

void collectRetiredDescriptorSets()
{
    auto it = g_retiredDescriptorSets.begin();
    while (it != g_retiredDescriptorSets.end()) {
        if (it->retireFrame + IMGUI_DESCRIPTOR_GC_DELAY_FRAMES > g_imguiFrameIndex) {
            ++it;
            continue;
        }
        ImGuiManager::removeTexture(it->ds);
        it = g_retiredDescriptorSets.erase(it);
    }
}

void pruneStaleImageCacheEntries()
{
    auto it = g_imageCache.begin();
    while (it != g_imageCache.end()) {
        if (it->second.lastUsedFrame + IMGUI_DESCRIPTOR_GC_DELAY_FRAMES > g_imguiFrameIndex) {
            ++it;
            continue;
        }
        if (it->second.ds) {
            ImGuiManager::removeTexture(it->second.ds);
        }
        it = g_imageCache.erase(it);
    }
}

void beginImageCacheFrame()
{
    ++g_imguiFrameIndex;
    collectRetiredDescriptorSets();
    pruneStaleImageCacheEntries();
}

void* getOrCreateDescriptorSet(IImageView* imageView, Sampler* sampler)
{
    if (!imageView || !sampler) {
        return nullptr;
    }

    ImageCacheKey   key{.imageView = imageView, .sampler = sampler};
    ImageViewHandle handle = imageView->getHandle();

    auto it = g_imageCache.find(key);
    if (it != g_imageCache.end()) {
        if (it->second.ds && it->second.handle == handle) {
            it->second.lastUsedFrame = g_imguiFrameIndex;
            return it->second.ds;
        }
        if (it->second.ds) {
            YA_CORE_TRACE("Invalidated ImGui descriptor set in cache, imageView: {}, sampler: {}. remove it",
                          handle.ptr,
                          sampler->getHandle().ptr);
            retireDescriptorSet(it->second.ds);
        }
    }

    void* ds = ImGuiManager::addTexture(imageView, sampler, EImageLayout::ShaderReadOnlyOptimal);
    if (!ds) {
        return nullptr;
    }

    g_imageCache[key] = ImageCacheEntry{
        .handle        = handle,
        .ds            = ds,
        .lastUsedFrame = g_imguiFrameIndex,
    };
    return ds;
}

} // namespace

void* ImGuiManager::addTexture(IImageView* imageView, Sampler* sampler, EImageLayout::T layout)
{
    if (!imageView || !sampler) {
        YA_CORE_ERROR("ImGuiManager::addTexture: Invalid imageView or sampler");
        return nullptr;
    }

    VkDescriptorSet ds = ImGui_ImplVulkan_AddTexture(
        sampler->getHandle().as<VkSampler>(),
        imageView->getHandle().as<VkImageView>(),
        toVk(layout));

    if (ds == VK_NULL_HANDLE) {
        YA_CORE_ERROR("ImGuiManager::addTexture: Failed to create descriptor set");
        return nullptr;
    }

    return static_cast<void*>(ds);
}

void ImGuiManager::removeTexture(void* textureID)
{
    if (!textureID) {
        return;
    }

    ImGui_ImplVulkan_RemoveTexture(static_cast<VkDescriptorSet>(textureID));
}

namespace ImGuiHelper
{

void BeginFrame()
{
    beginImageCacheFrame();
}

bool Image(IImageView*        imageView,
           Sampler*           sampler,
           const std::string& alt,
           const ImVec2&      size,
           const ImVec2&      uv0,
           const ImVec2&      uv1,
           const ImVec4&      tint,
           const ImVec4&      border)
{
    if (imageView && imageView->getHandle()) {
        void* ds = getOrCreateDescriptorSet(imageView, sampler);
        if (ds) {
            ImGui::Image(ds, size, uv0, uv1, tint, border);
            return true;
        }
    }
    ImGui::TextColored(ImVec4(1, 0, 0, 1), "Invalid Image: %s", alt.c_str());
    return false;
}

void ClearImageCache()
{
    for (auto& entry : g_imageCache) {
        if (entry.second.ds) {
            ImGuiManager::removeTexture(entry.second.ds);
        }
    }
    g_imageCache.clear();

    for (auto& entry : g_retiredDescriptorSets) {
        if (entry.ds) {
            ImGuiManager::removeTexture(entry.ds);
        }
    }
    g_retiredDescriptorSets.clear();
}

} // namespace ImGuiHelper

} // namespace ya
