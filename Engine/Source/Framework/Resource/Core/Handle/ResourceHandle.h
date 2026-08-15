#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>

namespace ya
{

/**
 * @brief Lightweight POD handle identifying a resource slot in a ResourceTable<T>.
 *
 * Resources are not objects: a handle is an *identity*, not a pointer. It stays
 * stable across hot-reload / streaming / GPU recreation because the underlying
 * object can be swapped in the table while the handle keeps pointing at the same
 * slot. The generation counter guards against the ABA problem: when a slot is
 * recycled, its generation is bumped so stale handles fail validation.
 *
 * Templated per resource type (FResourceHandle<Texture>, FResourceHandle<Model>)
 * so the compiler enforces type safety; no runtime type tag needed.
 *
 * 8-byte trivially-copyable POD. Do NOT serialize index/generation — they are
 * runtime-only. Serialize the asset path instead and re-resolve to a handle.
 */
template <typename T>
struct FResourceHandle
{
    static constexpr uint32_t kInvalidIndex = ~0u;

    uint32_t index      = kInvalidIndex;
    uint32_t generation = 0;

    [[nodiscard]] bool isValid() const { return index != kInvalidIndex; }

    bool operator==(const FResourceHandle &) const = default;
};

static_assert(sizeof(FResourceHandle<int>) == 8, "FResourceHandle must be an 8-byte POD");

} // namespace ya

namespace std
{

template <typename T>
struct hash<ya::FResourceHandle<T>>
{
    std::size_t operator()(const ya::FResourceHandle<T> &h) const noexcept
    {
        return (static_cast<std::size_t>(h.generation) << 32) ^ static_cast<std::size_t>(h.index);
    }
};

} // namespace std
