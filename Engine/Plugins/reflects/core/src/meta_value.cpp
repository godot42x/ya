#include "meta_value.h"

#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>
#include <new>
#include <stdexcept>
#include <utility>

namespace refl
{
namespace
{

struct WideInt
{
    bool     negative  = false;
    uint64_t magnitude = 0;
};

template <class Signed, class Unsigned>
bool readSized(bool bSigned, const void *p, WideInt &out)
{
    if (bSigned) {
        Signed value{};
        std::memcpy(&value, p, sizeof(value));
        if (value < 0) {
            out.negative = true;
            if constexpr (sizeof(Signed) == sizeof(int64_t)) {
                if (value == std::numeric_limits<int64_t>::min()) {
                    out.magnitude = static_cast<uint64_t>(std::numeric_limits<int64_t>::max()) + 1ull;
                }
                else {
                    out.magnitude = static_cast<uint64_t>(-value);
                }
            }
            else {
                out.magnitude = static_cast<uint64_t>(-static_cast<int64_t>(value));
            }
        }
        else {
            out.negative  = false;
            out.magnitude = static_cast<uint64_t>(value);
        }
        return true;
    }

    Unsigned value{};
    std::memcpy(&value, p, sizeof(value));
    out.negative  = false;
    out.magnitude = static_cast<uint64_t>(value);
    return true;
}

bool countsAsNegative(const WideInt &value)
{
    return value.negative && value.magnitude != 0;
}

bool readInt(const TypeOps &ops, const void *p, WideInt &out)
{
    if (ops.kind != EValueKind::Int || p == nullptr) {
        return false;
    }
    switch (ops.life.size) {
    case 1: {
        return readSized<int8_t, uint8_t>(ops.bSigned, p, out);
    }
    case 2: {
        return readSized<int16_t, uint16_t>(ops.bSigned, p, out);
    }
    case 4: {
        return readSized<int32_t, uint32_t>(ops.bSigned, p, out);
    }
    case 8: {
        return readSized<int64_t, uint64_t>(ops.bSigned, p, out);
    }
    default: {
        return false;
    }
    }
}

bool readDouble(const TypeOps &ops, const void *p, double &out)
{
    if (ops.kind != EValueKind::Float || p == nullptr) {
        return false;
    }
    if (ops.life.size == sizeof(float)) {
        float value = 0;
        std::memcpy(&value, p, sizeof(value));
        out = value;
        return true;
    }
    if (ops.life.size == sizeof(double)) {
        std::memcpy(&out, p, sizeof(double));
        return true;
    }
    return false;
}

bool doubleToWide(double value, WideInt &out)
{
    if (!std::isfinite(value) || std::trunc(value) != value) {
        return false;
    }
    // +0 and -0 are both non-negative. A negative flag with magnitude 0 is not a negative value.
    if (value == 0.0) {
        out.negative  = false;
        out.magnitude = 0;
        return true;
    }
    constexpr double kInt64Min = -9223372036854775808.0;
    constexpr double kU64Limit = 18446744073709551616.0;
    if (value < 0) {
        if (value < kInt64Min) {
            return false;
        }
        out.negative = true;
        if (value == kInt64Min) {
            out.magnitude = static_cast<uint64_t>(std::numeric_limits<int64_t>::max()) + 1ull;
        }
        else {
            out.magnitude = static_cast<uint64_t>(-value);
        }
        return true;
    }
    if (value >= kU64Limit) {
        return false;
    }
    out.negative  = false;
    out.magnitude = static_cast<uint64_t>(value);
    return true;
}

double wideToDouble(const WideInt &value)
{
    if (countsAsNegative(value)) {
        if (value.magnitude == static_cast<uint64_t>(std::numeric_limits<int64_t>::max()) + 1ull) {
            return -9223372036854775808.0;
        }
        return -static_cast<double>(value.magnitude);
    }
    return static_cast<double>(value.magnitude);
}

bool intFits(const TypeOps &dst, const WideInt &value)
{
    if (dst.life.size != 1 && dst.life.size != 2 && dst.life.size != 4 && dst.life.size != 8) {
        return false;
    }
    if (dst.bSigned) {
        if (countsAsNegative(value)) {
            const uint64_t limit = 1ull << (dst.life.size * 8 - 1);
            return value.magnitude <= limit;
        }
        if (dst.life.size == 8) {
            return value.magnitude <= static_cast<uint64_t>(std::numeric_limits<int64_t>::max());
        }
        const uint64_t limit = (1ull << (dst.life.size * 8 - 1)) - 1ull;
        return value.magnitude <= limit;
    }
    if (countsAsNegative(value)) {
        return false;
    }
    if (dst.life.size == 8) {
        return true;
    }
    const uint64_t limit = (1ull << (dst.life.size * 8)) - 1ull;
    return value.magnitude <= limit;
}

void writeInt(const TypeOps &dst, void *out, const WideInt &value)
{
    if (dst.bSigned) {
        int64_t signedValue = 0;
        if (countsAsNegative(value)) {
            const uint64_t minMagnitude = static_cast<uint64_t>(std::numeric_limits<int64_t>::max()) + 1ull;
            if (value.magnitude == minMagnitude) {
                signedValue = std::numeric_limits<int64_t>::min();
            }
            else {
                signedValue = -static_cast<int64_t>(value.magnitude);
            }
        }
        else {
            signedValue = static_cast<int64_t>(value.magnitude);
        }
        switch (dst.life.size) {
        case 1: {
            const auto stored = static_cast<int8_t>(signedValue);
            std::memcpy(out, &stored, 1);
            break;
        }
        case 2: {
            const auto stored = static_cast<int16_t>(signedValue);
            std::memcpy(out, &stored, 2);
            break;
        }
        case 4: {
            const auto stored = static_cast<int32_t>(signedValue);
            std::memcpy(out, &stored, 4);
            break;
        }
        case 8: {
            std::memcpy(out, &signedValue, 8);
            break;
        }
        default: {
            break;
        }
        }
        return;
    }

    switch (dst.life.size) {
    case 1: {
        const auto stored = static_cast<uint8_t>(value.magnitude);
        std::memcpy(out, &stored, 1);
        break;
    }
    case 2: {
        const auto stored = static_cast<uint16_t>(value.magnitude);
        std::memcpy(out, &stored, 2);
        break;
    }
    case 4: {
        const auto stored = static_cast<uint32_t>(value.magnitude);
        std::memcpy(out, &stored, 4);
        break;
    }
    case 8: {
        std::memcpy(out, &value.magnitude, 8);
        break;
    }
    default: {
        break;
    }
    }
}

bool writeFloat(const TypeOps &dst, void *out, double value)
{
    if (!std::isfinite(value)) {
        return false;
    }
    if (dst.life.size == sizeof(double)) {
        std::memcpy(out, &value, sizeof(double));
        return true;
    }
    if (dst.life.size == sizeof(float)) {
        const double limit = static_cast<double>(std::numeric_limits<float>::max());
        if (value > limit || value < -limit) {
            return false;
        }
        const float stored = static_cast<float>(value);
        if (!std::isfinite(stored)) {
            return false;
        }
        std::memcpy(out, &stored, sizeof(stored));
        return true;
    }
    return false;
}

bool loadWide(const MetaValue &value, WideInt &out, bool allowFloat)
{
    if (value.isNull() || value.data() == nullptr) {
        return false;
    }
    const TypeOps *ops = TypeRegistry::instance().find(value.type());
    if (ops == nullptr) {
        return false;
    }
    if (ops->kind == EValueKind::Int) {
        return readInt(*ops, value.data(), out);
    }
    if (allowFloat && ops->kind == EValueKind::Float) {
        double asDouble = 0;
        if (!readDouble(*ops, value.data(), asDouble)) {
            return false;
        }
        return doubleToWide(asDouble, out);
    }
    return false;
}

std::optional<int64_t> wideToInt64(const WideInt &value)
{
    if (countsAsNegative(value)) {
        const uint64_t minMagnitude = static_cast<uint64_t>(std::numeric_limits<int64_t>::max()) + 1ull;
        if (value.magnitude > minMagnitude) {
            return std::nullopt;
        }
        if (value.magnitude == minMagnitude) {
            return std::numeric_limits<int64_t>::min();
        }
        return -static_cast<int64_t>(value.magnitude);
    }
    if (value.magnitude > static_cast<uint64_t>(std::numeric_limits<int64_t>::max())) {
        return std::nullopt;
    }
    return static_cast<int64_t>(value.magnitude);
}

} // namespace

MetaValue::MetaValue() noexcept : _store{} {}

MetaValue::~MetaValue()
{
    reset();
}

MetaValue::MetaValue(const MetaValue &other) : _store{}
{
    copyFrom(other);
}

MetaValue::MetaValue(MetaValue &&other) noexcept : _store{}
{
    steal(other);
}

MetaValue &MetaValue::operator=(const MetaValue &other)
{
    if (this == &other) {
        return *this;
    }
    MetaValue tmp(other);
    reset();
    steal(tmp);
    return *this;
}

MetaValue &MetaValue::operator=(MetaValue &&other) noexcept
{
    if (this == &other) {
        return *this;
    }
    reset();
    steal(other);
    return *this;
}

MetaValue MetaValue::construct(type_index_t type)
{
    const TypeOps *ops = TypeRegistry::instance().find(type);
    if (ops == nullptr) {
        throw std::runtime_error("MetaValue::construct: type is not registered");
    }
    return construct(*ops);
}

MetaValue MetaValue::construct(const TypeOps &ops)
{
    const TypeOps *stable = TypeRegistry::instance().find(ops.type);
    if (stable == nullptr) {
        throw std::runtime_error("MetaValue::construct: type is not registered");
    }
    if (stable->life.defaultConstruct == nullptr) {
        throw std::runtime_error("MetaValue::construct: type is not default constructible");
    }
    if (metaValueFitsInline(*stable)) {
        MetaValue out;
        stable->life.defaultConstruct(out._store.inlineBytes);
        out._type    = stable->type;
        out._storage = EMetaStorage::Inline;
        return out;
    }
    void *mem = ::operator new(stable->life.size, std::align_val_t(stable->life.align));
    try {
        stable->life.defaultConstruct(mem);
    }
    catch (...) {
        ::operator delete(mem, std::align_val_t(stable->life.align));
        throw;
    }
    MetaValue out;
    out.adoptBoxed(stable->type, mem, &stable->life);
    return out;
}

MetaValue MetaValue::ref(type_index_t type, void *ptr, bool bConst)
{
    MetaValue out;
    out.adoptRef(type, ptr, bConst);
    return out;
}

MetaValue MetaValue::trivial(type_index_t type, const void *bytes, size_t size)
{
    if (type == 0 || bytes == nullptr || size == 0 || size > kMetaInlineSize) {
        throw std::runtime_error("MetaValue::trivial requires an inline value");
    }
    MetaValue out;
    out.adoptInline(type, bytes, size);
    return out;
}

void *MetaValue::data()
{
    if (_type == 0) {
        return nullptr;
    }
    switch (_storage) {
    case EMetaStorage::Inline: {
        return _store.inlineBytes;
    }
    case EMetaStorage::Boxed: {
        return _store.boxed;
    }
    case EMetaStorage::Ref: {
        return _store.ref;
    }
    }
    return nullptr;
}

const void *MetaValue::data() const
{
    if (_type == 0) {
        return nullptr;
    }
    switch (_storage) {
    case EMetaStorage::Inline: {
        return _store.inlineBytes;
    }
    case EMetaStorage::Boxed: {
        return _store.boxed;
    }
    case EMetaStorage::Ref: {
        return _store.ref;
    }
    }
    return nullptr;
}

MetaValue MetaValue::materialize() const
{
    if (_storage != EMetaStorage::Ref) {
        return *this;
    }
    const TypeOps *ops = TypeRegistry::instance().find(_type);
    if (ops == nullptr || ops->life.copyConstruct == nullptr || _store.ref == nullptr) {
        throw std::runtime_error("MetaValue::materialize requires copyConstruct");
    }
    if (metaValueFitsInline(*ops)) {
        MetaValue out;
        ops->life.copyConstruct(out._store.inlineBytes, _store.ref);
        out._type    = ops->type;
        out._storage = EMetaStorage::Inline;
        return out;
    }
    void *mem = ::operator new(ops->life.size, std::align_val_t(ops->life.align));
    try {
        ops->life.copyConstruct(mem, _store.ref);
    }
    catch (...) {
        ::operator delete(mem, std::align_val_t(ops->life.align));
        throw;
    }
    MetaValue out;
    out.adoptBoxed(ops->type, mem, &ops->life);
    return out;
}

std::optional<int64_t> MetaValue::toInt64() const
{
    WideInt wide;
    if (!loadWide(*this, wide, true)) {
        return std::nullopt;
    }
    return wideToInt64(wide);
}

std::optional<uint64_t> MetaValue::toUInt64() const
{
    WideInt wide;
    if (!loadWide(*this, wide, true)) {
        return std::nullopt;
    }
    if (countsAsNegative(wide)) {
        return std::nullopt;
    }
    return wide.magnitude;
}

std::optional<double> MetaValue::toDouble() const
{
    if (isNull() || data() == nullptr) {
        return std::nullopt;
    }
    const TypeOps *ops = TypeRegistry::instance().find(_type);
    if (ops == nullptr) {
        return std::nullopt;
    }
    if (ops->kind == EValueKind::Float) {
        double value = 0;
        if (!readDouble(*ops, data(), value)) {
            return std::nullopt;
        }
        return value;
    }
    if (ops->kind == EValueKind::Int) {
        WideInt wide;
        if (!readInt(*ops, data(), wide)) {
            return std::nullopt;
        }
        return wideToDouble(wide);
    }
    return std::nullopt;
}

std::optional<bool> MetaValue::toBool() const
{
    if (isNull() || data() == nullptr) {
        return std::nullopt;
    }
    const TypeOps *ops = TypeRegistry::instance().find(_type);
    if (ops == nullptr || ops->kind != EValueKind::Bool || ops->life.size != sizeof(bool)) {
        return std::nullopt;
    }
    bool value = false;
    std::memcpy(&value, data(), sizeof(bool));
    return value;
}

const std::string *MetaValue::asString() const
{
    if (isNull() || data() == nullptr) {
        return nullptr;
    }
    const TypeOps *ops = TypeRegistry::instance().find(_type);
    if (ops == nullptr || ops->kind != EValueKind::String || ops->life.size != sizeof(std::string)) {
        return nullptr;
    }
    return static_cast<const std::string *>(data());
}

static bool convertChecked(const TypeOps &src, const TypeOps &dst, const void *in, void *out)
{
    if (src.type == dst.type) {
        if (src.bTriviallyCopyable) {
            std::memcpy(out, in, src.life.size);
            return true;
        }
        return false;
    }
    if (src.kind == EValueKind::Bool || dst.kind == EValueKind::Bool) {
        return false;
    }
    if (src.kind == EValueKind::Int && dst.kind == EValueKind::Int) {
        WideInt wide;
        if (!readInt(src, in, wide) || !intFits(dst, wide)) {
            return false;
        }
        writeInt(dst, out, wide);
        return true;
    }
    if (src.kind == EValueKind::Int && dst.kind == EValueKind::Float) {
        WideInt wide;
        if (!readInt(src, in, wide)) {
            return false;
        }
        return writeFloat(dst, out, wideToDouble(wide));
    }
    if (src.kind == EValueKind::Float && dst.kind == EValueKind::Float) {
        double value = 0;
        if (!readDouble(src, in, value)) {
            return false;
        }
        return writeFloat(dst, out, value);
    }
    if (src.kind == EValueKind::Float && dst.kind == EValueKind::Int) {
        double value = 0;
        if (!readDouble(src, in, value)) {
            return false;
        }
        WideInt wide;
        if (!doubleToWide(value, wide) || !intFits(dst, wide)) {
            return false;
        }
        writeInt(dst, out, wide);
        return true;
    }
    return false;
}

bool MetaValue::convertTo(type_index_t target, void *out) const
{
    if (out == nullptr || isNull() || data() == nullptr) {
        return false;
    }
    const TypeOps *src = TypeRegistry::instance().find(_type);
    const TypeOps *dst = TypeRegistry::instance().find(target);
    if (src == nullptr || dst == nullptr) {
        return false;
    }
    return convertChecked(*src, *dst, data(), out);
}

bool MetaValue::convertTo(const TypeOps &src, const TypeOps &dst, void *out) const
{
    if (out == nullptr || isNull() || data() == nullptr || src.type != _type) {
        return false;
    }
    return convertChecked(src, dst, data(), out);
}

void MetaValue::reset() noexcept
{
    void           *boxed = nullptr;
    const MetaLife *life  = nullptr;
    if (_storage == EMetaStorage::Boxed) {
        boxed = _store.boxed;
        life  = _life;
    }
    _type    = 0;
    _storage = EMetaStorage::Inline;
    _bConst  = false;
    _life    = nullptr;
    _store   = Store{};
    if (boxed != nullptr && life != nullptr) {
        if (life->destroy != nullptr) {
            life->destroy(boxed);
        }
        ::operator delete(boxed, std::align_val_t(life->align));
    }
}

void MetaValue::copyFrom(const MetaValue &other)
{
    switch (other._storage) {
    case EMetaStorage::Inline: {
        adoptInline(other._type, other._store.inlineBytes, kMetaInlineSize);
        break;
    }
    case EMetaStorage::Ref: {
        adoptRef(other._type, other._store.ref, other._bConst);
        break;
    }
    case EMetaStorage::Boxed: {
        if (other._life == nullptr || other._life->copyConstruct == nullptr || other._store.boxed == nullptr) {
            throw std::runtime_error("MetaValue copy requires copyConstruct");
        }
        void *mem = ::operator new(other._life->size, std::align_val_t(other._life->align));
        try {
            other._life->copyConstruct(mem, other._store.boxed);
        }
        catch (...) {
            ::operator delete(mem, std::align_val_t(other._life->align));
            throw;
        }
        adoptBoxed(other._type, mem, other._life);
        break;
    }
    }
}

void MetaValue::steal(MetaValue &other) noexcept
{
    _type    = other._type;
    _bConst  = other._bConst;
    _life    = other._life;
    _storage = other._storage;
    switch (other._storage) {
    case EMetaStorage::Inline: {
        _store = other._store;
        break;
    }
    case EMetaStorage::Boxed: {
        _store.boxed = other._store.boxed;
        break;
    }
    case EMetaStorage::Ref: {
        _store.ref = other._store.ref;
        break;
    }
    }
    other._type    = 0;
    other._bConst  = false;
    other._life    = nullptr;
    other._storage = EMetaStorage::Inline;
    other._store   = Store{};
}

void MetaValue::adoptInline(type_index_t type, const void *src, size_t size)
{
    _store = Store{};
    if (src != nullptr && size > 0) {
        std::memcpy(_store.inlineBytes, src, size);
    }
    _type    = type;
    _storage = EMetaStorage::Inline;
    _bConst  = false;
    _life    = nullptr;
}

void MetaValue::adoptBoxed(type_index_t type, void *mem, const MetaLife *life)
{
    _store.boxed = mem;
    _type        = type;
    _storage     = EMetaStorage::Boxed;
    _bConst      = false;
    _life        = life;
}

void MetaValue::adoptRef(type_index_t type, void *ptr, bool bConst)
{
    _store.ref = ptr;
    _type      = type;
    _storage   = EMetaStorage::Ref;
    _bConst    = bConst;
    _life      = nullptr;
}

} // namespace refl
