#pragma once

#include <memory>
#include <string_view>
#include <typeindex>

namespace ya
{

/**
 * @brief Type-erased handle for a resource that must outlive a GPU submission.
 *
 * Replaces the bare `std::shared_ptr<void>` previously used across the
 * retained-resource contract. The wrapped `shared_ptr<void>` still owns the
 * original deleter (so destruction is correct), while the recorded
 * `std::type_index` restores compile-time type information for safe, checked
 * extraction via `as<T>()` and for diagnostics.
 *
 * The retainable resource set is open-ended (IImage, IImageView, IBuffer,
 * ImageResource, Texture, ...), so we deliberately use a type index rather
 * than a `std::variant` — new resource kinds need no declaration change.
 */
struct RetainedResource
{
    std::shared_ptr<void> resource;
    std::type_index       type     = std::type_index(typeid(void));
    std::string_view      debugTag = {}; ///< Diagnostic tag; must outlive this handle.

    RetainedResource() = default;

    /// Construct from any shared_ptr<T>, deducing the stored type.
    template <typename T>
    RetainedResource(std::shared_ptr<T> res, std::string_view tag = {})
        : resource(std::move(res))
        , type(std::type_index(typeid(T)))
        , debugTag(tag)
    {}

    /// Raw pointer (as void*); kept for backward-compatible `.get()` comparisons.
    [[nodiscard]] void* get() const { return resource.get(); }

    /// Typed pointer, or nullptr if the stored type is not T.
    template <typename T>
    [[nodiscard]] T* as() const
    {
        return type == std::type_index(typeid(T))
            ? static_cast<T*>(resource.get())
            : nullptr;
    }

    explicit operator bool() const { return static_cast<bool>(resource); }
};

} // namespace ya
