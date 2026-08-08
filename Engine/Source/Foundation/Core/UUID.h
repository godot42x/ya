
#pragma once

#include <cstdint>
#include <functional>
namespace ya
{

struct UUID
{
    uint64_t value;

    UUID();
    explicit UUID(uint64_t val) : value(val) {}

    operator uint64_t() const { return value; }

    bool operator==(const UUID &other) const { return value == other.value; }
    bool operator!=(const UUID &other) const { return value != other.value; }
    bool operator<(const UUID &other) const { return value < other.value; }
};

} // namespace ya


namespace std
{
template <>
struct hash<ya::UUID>
{
    std::size_t operator()(const ya::UUID &uuid) const noexcept
    {
        return std::hash<uint64_t>{}(uuid.value);
    }
};
} // namespace std