
#pragma once

#include "Core/Reflection/Reflection.h"
#include "glm/glm.hpp"

namespace ya
{
// Generic render types
struct Extent2D
{
    uint32_t width{0};
    uint32_t height{0};

    glm::vec2       toVec2() const { return glm::vec2(width, height); }
    static Extent2D fromVec2(glm::vec2 v)
    {
        return Extent2D{
            .width  = static_cast<uint32_t>(v.x),
            .height = static_cast<uint32_t>(v.y),
        };
    }
};



struct FAssetPath
{
    YA_REFLECT_BEGIN(FAssetPath)
    YA_REFLECT_FIELD(path)
    YA_REFLECT_END()
    std::string path;
};

struct FSoftObjectReference
{
    YA_REFLECT_BEGIN(FSoftObjectReference)
    YA_REFLECT_FIELD(assetPath)
    YA_REFLECT_FIELD(object)
    YA_REFLECT_END()

    FAssetPath assetPath;
    void      *object;
};


template <typename T>
concept CDefault = requires { { T::defaults() } -> std::same_as<T>; };

template <typename T>
using stdptr = std::shared_ptr<T>;


template <typename T>
struct Ptr
{
    // 2025/1/31 no sure to use shared ptr in some place? make a wrapper first

    // using type = stdptr<T>;
    using type = T *;

    type v;

    Ptr() : v(nullptr) {}

    Ptr(T *p) : v(p) {}
    Ptr(const stdptr<T> &p) : v(p.get()) {}

    T *get() const { return v; }

    type operator->() { return v; }
    type operator->() const { return v; }

    operator T *() const { return v; }

    explicit operator bool() const { return v != nullptr; }
    void     reset()
    {
        if constexpr (requires { type::reset(); }) {
            v->reset();
        }
        else {
            v = nullptr;
        }
    }
};


template <typename T, typename... Args>
std::shared_ptr<T> makeShared(Args &&...args)
    // requires requires(T, Args... args) { new T(std::forward<Args>(args)...); }
    requires std::is_constructible_v<T, Args...>
{
    return std::make_shared<T>(std::forward<Args>(args)...);
}

template <typename T, typename... Args>
std::unique_ptr<T> makeUnique(Args &&...args)
    requires std::is_constructible_v<T, Args...>
{
    return std::make_unique<T>(std::forward<Args>(args)...);
}

using stdpath  = std::filesystem::path;
using stdclock = std::chrono::steady_clock;
using namespace std::string_literals;
using namespace std::literals;

// #define NAMESPACE_BEGIN(name) \
//     namespace name            \
//     {
// #define NAMESPACE_END(name) }

struct plat_base_tag
{};

template <typename Base>
struct plat_base : public plat_base_tag
{

    template <typename Derived>
    Derived *as()
    {
        static_assert(std::is_base_of_v<Base, Derived>, "T must be derived from plat_base");
        return static_cast<Derived *>(this);
    }

    // template <typename Derived>
    // stdptr<Derived> asShared()
    // {
    //     static_assert(std::is_base_of_v<Base, Derived>, "T must be derived from plat_base");
    //     return static_pointer_cast<Derived>(shared_from_this());
    // }

    virtual ~plat_base() = default;
};

} // namespace ya