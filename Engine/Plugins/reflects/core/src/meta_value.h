#pragma once

#include "type_ops.h"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <new>
#include <optional>
#include <string>
#include <type_traits>
#include <utility>

namespace refl
{

inline constexpr size_t kMetaInlineSize  = 32;
inline constexpr size_t kMetaInlineAlign = 16;

enum class EMetaStorage : uint8_t
{
    Inline,
    Boxed,
    Ref
};

template <class T>
constexpr bool metaValueFitsInline()
{
    return std::is_trivially_copyable_v<T> && sizeof(T) <= kMetaInlineSize && alignof(T) <= kMetaInlineAlign;
}

inline bool metaValueFitsInline(const TypeOps &ops)
{
    return ops.bTriviallyCopyable && ops.life.size > 0 && ops.life.size <= kMetaInlineSize && ops.life.align <= kMetaInlineAlign;
}

// 每 T 一张，make<T> 走这里，不查注册表。只用来调用，不要拿地址当类型身份。
template <class T>
struct MetaBoxedLife
{
    static void copy(void *dst, const void *src) { new (dst) T(*static_cast<const T *>(src)); }
    static void move(void *dst, void *src) { new (dst) T(std::move(*static_cast<T *>(src))); }
    static void destroy(void *obj) { static_cast<T *>(obj)->~T(); }

    static const MetaLife &table()
    {
        static const MetaLife life{
            .size           = sizeof(T),
            .align          = alignof(T),
            .copyConstruct  = &copy,
            .moveConstruct  = &move,
            .destroy        = &destroy,
        };
        return life;
    }
};

class REFLECTS_CORE_API MetaValue
{
    type_index_t _type    = 0;
    EMetaStorage _storage = EMetaStorage::Inline;
    bool         _bConst  = false;
    union Store
    {
        alignas(16) std::byte inlineBytes[kMetaInlineSize];
        void                 *boxed;
        void                 *ref;
    } _store;
    const MetaLife *_life = nullptr;

  public:
    MetaValue() noexcept;
    ~MetaValue();
    MetaValue(const MetaValue &other);
    MetaValue(MetaValue &&other) noexcept;
    MetaValue &operator=(const MetaValue &other);
    MetaValue &operator=(MetaValue &&other) noexcept;

    template <class T>
    static MetaValue make(T value);

    static MetaValue construct(type_index_t type);
    static MetaValue construct(const TypeOps &ops);

    template <class T>
    static MetaValue ref(T &obj);
    template <class T>
    static MetaValue cref(const T &obj);
    static MetaValue ref(type_index_t type, void *ptr, bool bConst);
    // Copies `size` bytes of a trivially copyable value. No registry lookup.
    // `size` must be in (0, kMetaInlineSize].
    static MetaValue trivial(type_index_t type, const void *bytes, size_t size);

    template <class T>
    T *tryGet();
    template <class T>
    const T *tryGet() const;

    void       *data();
    const void *data() const;
    bool         isNull() const { return _type == 0; }
    bool         operator==(const MetaValue& other) const;
    bool         isRef() const { return _type != 0 && _storage == EMetaStorage::Ref; }
    bool         isConst() const { return _bConst; }
    EMetaStorage storage() const { return _storage; }
    type_index_t type() const { return _type; }

    MetaValue materialize() const;

    std::optional<int64_t>  toInt64() const;
    std::optional<uint64_t> toUInt64() const;
    std::optional<double>   toDouble() const;
    std::optional<bool>     toBool() const;
    const std::string      *asString() const;

    template <class T>
    std::optional<T> convert() const;

    // out 指向目标类型的已构造对象。同类型只对可平凡拷贝类型 memcpy；
    // 非平凡同类型返回 false，调用方用 tryGet / materialize。
    bool convertTo(type_index_t target, void *out) const;
    // `src` 必须就是这个值的 TypeOps（src.type == type()）。跳过注册表查找。
    bool convertTo(const TypeOps &src, const TypeOps &dst, void *out) const;

  private:
    void reset() noexcept;
    void copyFrom(const MetaValue &other);
    void steal(MetaValue &other) noexcept;

    void adoptInline(type_index_t type, const void *src, size_t size);
    void adoptBoxed(type_index_t type, void *mem, const MetaLife *life);
    void adoptRef(type_index_t type, void *ptr, bool bConst);
};

static_assert(sizeof(MetaValue) <= 64, "MetaValue must stay within 64 bytes");
static_assert(alignof(MetaValue) >= kMetaInlineAlign, "inline storage must be 16-byte aligned");

template <class T>
MetaValue MetaValue::make(T value)
{
    using U = std::decay_t<T>;
    if constexpr (metaValueFitsInline<U>()) {
        MetaValue out;
        out.adoptInline(type_index_v<U>, &value, sizeof(U));
        return out;
    }
    else {
        static_assert(std::is_copy_constructible_v<U>, "Boxed MetaValue requires a copyable type");
        const MetaLife &life = MetaBoxedLife<U>::table();
        void           *mem  = ::operator new(sizeof(U), std::align_val_t(alignof(U)));
        try {
            new (mem) U(std::move(value));
        }
        catch (...) {
            ::operator delete(mem, std::align_val_t(alignof(U)));
            throw;
        }
        MetaValue out;
        out.adoptBoxed(type_index_v<U>, mem, &life);
        return out;
    }
}

template <class T>
MetaValue MetaValue::ref(T &obj)
{
    using U = std::remove_cv_t<T>;
    return ref(type_index_v<U>, const_cast<U *>(&obj), std::is_const_v<T>);
}

template <class T>
MetaValue MetaValue::cref(const T &obj)
{
    using U = std::remove_cv_t<T>;
    return ref(type_index_v<U>, const_cast<U *>(&obj), true);
}

template <class T>
T *MetaValue::tryGet()
{
    using U = std::remove_cv_t<T>;
    if (_type == 0 || _type != type_index_v<U>) {
        return nullptr;
    }
    if (_storage == EMetaStorage::Ref && _bConst) {
        return nullptr;
    }
    return static_cast<T *>(data());
}

template <class T>
const T *MetaValue::tryGet() const
{
    using U = std::remove_cv_t<T>;
    if (_type == 0 || _type != type_index_v<U>) {
        return nullptr;
    }
    return static_cast<const T *>(data());
}

template <class T>
std::optional<T> MetaValue::convert() const
{
    using U = std::remove_cv_t<T>;
    if (isNull()) {
        return std::nullopt;
    }
    if (_type == type_index_v<U>) {
        if (const U *value = tryGet<U>()) {
            return *value;
        }
        return std::nullopt;
    }
    U out{};
    if (!convertTo(type_index_v<U>, &out)) {
        return std::nullopt;
    }
    return out;
}

} // namespace refl
