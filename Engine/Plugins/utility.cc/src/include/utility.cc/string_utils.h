/*
 * @ Author: godot42
 * @ Create Time: 2025-01-06 21:14:30
 * @ Modified by: @godot42
 * @ Modified by: @godot42
 * @ Modified time: 2025-03-22 00:31:12
 */


#pragma once

#include <format>
#include <string>
#include <string_view>
#include <vector>


#include "plat.h"

namespace ut
{



namespace str
{
UTILITY_CC_API std::string replace(std::string_view source, std::string_view from, const std::string_view to);
UTILITY_CC_API std::vector<std::string> split(std::string_view source, char delimiter = ' ');
UTILITY_CC_API bool                     split(std::string_view source, char sep, std::string &left, std::string_view &right);
UTILITY_CC_API std::string_view left(std::string_view source, std::string_view delimiter);
UTILITY_CC_API std::string_view trim(std::string_view source);
UTILITY_CC_API std::string toLower(std::string_view source);
UTILITY_CC_API std::string toUpper(std::string_view source);
UTILITY_CC_API std::string concat(std::vector<std::string_view> source, const std::string_view delimiter = "");

template <typename T>
std::string join(const T &container, const std::string_view delimiter = "")
{
    std::string result;
    for (auto it = container.begin(); it != container.end(); ++it)
    {
        if (it != container.begin())
        {
            result += delimiter;
        }
        // Always format. The old requires-expression is ill-formed on
        // libstdc++, and `if (!it)` does not compile for standard iterators.
        result += std::format("{}", *it);
    }
    return result;
}
} // namespace str


} // namespace ut