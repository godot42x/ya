#pragma once

#include "imgui.h"
#include "Render/Core/DescriptorSet.h"
#include "Render/Core/Image.h"

// MSVC Annex K (bounds-checked) string APIs are not portable. On macOS/Linux
// route strncpy_s to a safe bounded copy and map _TRUNCATE to a sentinel that
// still produces a valid result. We intentionally only implement the two
// signatures used in this codebase (four-arg explicit size and two-arg array
// form) so any new caller would fail to compile and prompt a review.
#if !defined(_WIN32)
    #include <cstring>
    #ifndef _TRUNCATE
        #define _TRUNCATE ((size_t)-1)
    #endif
    inline int strncpy_s(char *dest, size_t destSize, const char *src, size_t count)
    {
        if (!dest || destSize == 0) return 22; // EINVAL
        if (!src) { dest[0] = '\0'; return 22; }
        size_t copyCount = (count == _TRUNCATE) ? destSize - 1 : count;
        if (copyCount >= destSize) copyCount = destSize - 1;
        std::memcpy(dest, src, copyCount);
        dest[copyCount] = '\0';
        return 0;
    }
    template <size_t N>
    inline int strncpy_s(char (&dest)[N], const char *src, size_t count)
    {
        return strncpy_s(dest, N, src, count);
    }
#endif

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
