#pragma once
#include <cstddef>
#include <functional>


namespace ut
{


template <typename T>
void hashCombined(std::size_t &seed, const T &v)
{
    std::hash<T> hasher;
    seed ^= hasher(v) + 0x9e3779b9 + (seed << 6) + (seed >> 2);
}

} // namespace ut