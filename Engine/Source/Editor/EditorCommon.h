#pragma once

#include "ImGui.h"
#include "Render/Core/DescriptorSet.h"
#include "Render/Core/Image.h"

namespace ya
{

struct ImGuiImageEntry
{
    ya::Ptr<IImageView> imageView;
    ya::Ptr<Sampler>    sampler;
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

// Unified context menu interface
class ContextMenu
{
  public:
    enum class Type
    {
        BlankSpace, // Right-click on empty area
        EntityItem, // Right-click on entity/item
    };

    ContextMenu(std::string_view id, Type type, ImGuiPopupFlags flags = 0)
        : _id(id), _type(type), _flags(flags)
    {
    }

    virtual ~ContextMenu() = default;

    // Begin context menu rendering - returns true if menu should be rendered
    bool begin()
    {
        if (_type == Type::BlankSpace) {
            return ImGui::BeginPopupContextWindow(
                _id.c_str(),
                _flags | ImGuiPopupFlags_NoOpenOverItems | ImGuiPopupFlags_MouseButtonRight);
        }
        else {
            return ImGui::BeginPopupContextItem(_id.c_str(), ImGuiPopupFlags_MouseButtonRight);
        }
    }

    // End context menu rendering
    void end()
    {
        ImGui::EndPopup();
    }

    // Helper to render menu items with a callback
    bool menuItem(std::string_view label, std::string_view shortcut = "", bool *selected = nullptr)
    {
        return ImGui::MenuItem(label.data(), shortcut.empty() ? nullptr : shortcut.data(), selected);
    }

    void separator()
    {
        ImGui::Separator();
    }

    bool beginMenu(std::string_view label)
    {
        return ImGui::BeginMenu(label.data());
    }

    void endMenu()
    {
        ImGui::EndMenu();
    }

    Type getType() const { return _type; }

  private:
    std::string     _id;
    Type            _type;
    ImGuiPopupFlags _flags;
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
