#include "GameEditor/EditorLayerInternal.h"

#include "GameRuntime/GUI/GuiSystem.h"

namespace ya
{

const ImGuiImageEntry* EditorLayer::getOrCreateImGuiTextureID(ya::Ptr<IImageView> imageView, ya::Ptr<Sampler> sampler)
{
    YA_PROFILE_FUNCTION();
    if (!imageView) {
        YA_CORE_WARN("EditorLayer::getOrCreateImGuiTextureID: Invalid imageView or sampler");
        return nullptr;
    }
    if (!sampler) {
        sampler = TextureLibrary::get().getDefaultSampler();
    }

    ImGuiImageEntry entry{
        .imageView = imageView,
        .sampler   = sampler,
        .ds        = {},
    };
    auto it = _imguiTextureCache.find(entry);
    if (it != _imguiTextureCache.end()) {
        if (it->ds != nullptr) {
            return &(*it);
        }
    }

    void* textureID = GuiSystem::get().addTexture(imageView.get(), sampler.get(), EImageLayout::ShaderReadOnlyOptimal);
    if (!textureID) {
        YA_CORE_ERROR("EditorLayer::getOrCreateImGuiTextureID: Failed to create descriptor set");
        return nullptr;
    }

    entry.ds = textureID;
    _imguiTextureCache.insert(entry);
    YA_CORE_TRACE("Created ImGui descriptor set for imageView: {}", imageView->getHandle().ptr);

    return &(*_imguiTextureCache.find(entry));
}

void EditorLayer::cleanupImGuiTextures()
{
    YA_CORE_INFO("EditorLayer::cleanupImGuiTextures - Releasing {} descriptor sets", _imguiTextureCache.size());

    for (auto& entry : _imguiTextureCache) {
        if (entry.ds) {
            GuiSystem::get().removeTexture(entry.ds);
        }
    }
    _imguiTextureCache.clear();
}

void EditorLayer::removeImGuiTexture(const ImGuiImageEntry* entry)
{
    GuiSystem::get().removeTexture(entry->ds);
    _imguiTextureCache.erase(*entry);
}

} // namespace ya
