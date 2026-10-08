#pragma once

#include "reflects-core/api.h"
#include "type_index.h"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <new>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <vector>

namespace refl
{

enum class EValueKind : uint8_t
{
    Null,
    Bool,
    Int,
    Float,
    String,
    Enum,
    Object
};

enum class EFacetMissing : uint8_t
{
    Error,
    Fallback,
    Skip
};

// 只用于调用构造、拷贝、移动和析构。类型身份只看 type，不要比较这张表的地址。
// size / align 只放在这里，TypeOps 不再另存一份。
struct MetaLife
{
    size_t size  = 0;
    size_t align = 0;
    void (*copyConstruct)(void *dst, const void *src) = nullptr;
    void (*moveConstruct)(void *dst, void *src)       = nullptr;
    void (*destroy)(void *obj)                        = nullptr;
    void (*defaultConstruct)(void *dst)               = nullptr;
};

struct TypeOps
{
    type_index_t type = 0;
    std::string  name;
    // 仅诊断。指向 compilerTypeName 的静态存储，不得写入场景、缓存或其它持久化数据。
    // registerType<T> 填写；registerTypeRaw 可留空。
    std::string_view compilerName;
    EValueKind       kind               = EValueKind::Object;
    bool             bSigned            = false;
    bool             bTriviallyCopyable = false;
    MetaLife         life{};

    TypeOps()                               = default;
    TypeOps(TypeOps &&) noexcept            = default;
    TypeOps &operator=(TypeOps &&) noexcept = default;
    TypeOps(const TypeOps &)                = delete;
    TypeOps &operator=(const TypeOps &)     = delete;

  private:
    struct FacetEntry
    {
        type_index_t facetType = 0;
        void        *data      = nullptr;
        void (*destroy)(void *) = nullptr;

        FacetEntry() = default;
        FacetEntry(const FacetEntry &) = delete;
        FacetEntry &operator=(const FacetEntry &) = delete;

        FacetEntry(FacetEntry &&other) noexcept
            : facetType(other.facetType), data(other.data), destroy(other.destroy)
        {
            other.facetType = 0;
            other.data      = nullptr;
            other.destroy   = nullptr;
        }

        FacetEntry &operator=(FacetEntry &&other) noexcept
        {
            if (this == &other) {
                return *this;
            }
            reset();
            facetType       = other.facetType;
            data            = other.data;
            destroy         = other.destroy;
            other.facetType = 0;
            other.data      = nullptr;
            other.destroy   = nullptr;
            return *this;
        }

        ~FacetEntry() { reset(); }

        void reset() noexcept
        {
            if (destroy != nullptr && data != nullptr) {
                destroy(data);
            }
            facetType = 0;
            data      = nullptr;
            destroy   = nullptr;
        }
    };

    std::vector<FacetEntry> facets;

    friend class TypeRegistry;
};

// 注册只在主线程进行，注册表不加锁。
class TypeRegistry
{
    // unique_ptr：rehash 不搬动 TypeOps，缓存的 TypeOps* 在进程内保持有效。
    std::unordered_map<type_index_t, std::unique_ptr<TypeOps>> _byType;
    std::unordered_map<std::string, type_index_t> _byName;

  public:
    std::function<void(type_index_t type, std::string_view facetName)> missingFacetHook;

    TypeRegistry(const TypeRegistry &)            = delete;
    TypeRegistry &operator=(const TypeRegistry &) = delete;

    static REFLECTS_CORE_API TypeRegistry &instance();

    template <class T>
    TypeOps &registerType(std::string name);

    REFLECTS_CORE_API TypeOps &registerTypeRaw(TypeOps ops);

    REFLECTS_CORE_API const TypeOps *find(type_index_t type) const;
    REFLECTS_CORE_API const TypeOps *findByName(std::string_view name) const;

    template <class F>
    void setFacet(type_index_t type, F facet);

    template <class F>
    const F *facet(type_index_t type) const;

    template <class F>
    const F *resolveFacet(type_index_t type) const;

  private:
    TypeRegistry();
    void registerBuiltins();

    REFLECTS_CORE_API void setFacetErased(type_index_t type, type_index_t facetType, void *data, void (*destroy)(void *));
    REFLECTS_CORE_API const void *findFacet(type_index_t type, type_index_t facetType) const;
};

namespace detail
{

template <class T>
TypeOps makeTypeOps(std::string name)
{
    TypeOps ops;
    ops.type               = type_index_v<T>;
    ops.name               = std::move(name);
    ops.life.size          = sizeof(T);
    ops.life.align         = alignof(T);
    ops.bTriviallyCopyable = std::is_trivially_copyable_v<T>;

    if constexpr (std::is_same_v<T, bool>) {
        ops.kind = EValueKind::Bool;
    }
    else if constexpr (std::is_same_v<T, std::string>) {
        ops.kind = EValueKind::String;
    }
    else if constexpr (std::is_enum_v<T>) {
        ops.kind    = EValueKind::Enum;
        ops.bSigned = std::is_signed_v<std::underlying_type_t<T>>;
    }
    else if constexpr (std::is_integral_v<T>) {
        ops.kind    = EValueKind::Int;
        ops.bSigned = std::is_signed_v<T>;
    }
    else if constexpr (std::is_floating_point_v<T>) {
        ops.kind = EValueKind::Float;
    }
    else {
        ops.kind = EValueKind::Object;
    }

    if constexpr (std::is_default_constructible_v<T>) {
        ops.life.defaultConstruct = [](void *dst) { new (dst) T(); };
    }
    if constexpr (std::is_copy_constructible_v<T>) {
        ops.life.copyConstruct = [](void *dst, const void *src) { new (dst) T(*static_cast<const T *>(src)); };
    }
    if constexpr (std::is_move_constructible_v<T>) {
        ops.life.moveConstruct = [](void *dst, void *src) { new (dst) T(std::move(*static_cast<T *>(src))); };
    }
    if constexpr (std::is_destructible_v<T> && !std::is_trivially_destructible_v<T>) {
        ops.life.destroy = [](void *obj) { static_cast<T *>(obj)->~T(); };
    }
    return ops;
}

template <class F>
constexpr EFacetMissing facetMissing()
{
    if constexpr (requires { F::missing; }) {
        static_assert(std::is_same_v<std::decay_t<decltype(F::missing)>, EFacetMissing>,
                      "facet F must declare static constexpr refl::EFacetMissing missing");
        return F::missing;
    }
    else {
        static_assert(sizeof(F) == 0, "facet F must declare static constexpr refl::EFacetMissing missing");
        return EFacetMissing::Error;
    }
}

} // namespace detail

template <class T>
TypeOps &TypeRegistry::registerType(std::string name)
{
    TypeOps ops      = detail::makeTypeOps<T>(std::move(name));
    ops.compilerName = detail::compilerTypeName<T>();
    return registerTypeRaw(std::move(ops));
}

template <class F>
void TypeRegistry::setFacet(type_index_t type, F facet)
{
    F *copy = new F(std::move(facet));
    setFacetErased(type, type_index_v<F>, copy, [](void *p) { delete static_cast<F *>(p); });
}

template <class F>
const F *TypeRegistry::facet(type_index_t type) const
{
    return static_cast<const F *>(findFacet(type, type_index_v<F>));
}

template <class F>
const F *TypeRegistry::resolveFacet(type_index_t type) const
{
    constexpr EFacetMissing policy = detail::facetMissing<F>();
    if (const F *found = facet<F>(type)) {
        return found;
    }
    if constexpr (policy == EFacetMissing::Error) {
        if (missingFacetHook) {
            missingFacetHook(type, detail::compilerTypeName<F>());
        }
        return nullptr;
    }
    else if constexpr (policy == EFacetMissing::Fallback) {
        static_assert(std::is_lvalue_reference_v<decltype(F::fallback())>,
                      "Fallback facet must provide static const F& fallback()");
        return std::addressof(F::fallback());
    }
    else {
        return nullptr;
    }
}

} // namespace refl
