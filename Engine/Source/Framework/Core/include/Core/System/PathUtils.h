#pragma once

#include <filesystem>
#include <string>
#include <string_view>

namespace ya::path_utils
{

inline std::string utf8FromU8(std::u8string_view value)
{
    std::string utf8;
    utf8.reserve(value.size());
    for (char8_t ch : value) {
        utf8.push_back(static_cast<char>(ch));
    }
    return utf8;
}

inline std::u8string u8FromUtf8(std::string_view value)
{
    std::u8string native;
    native.reserve(value.size());
    for (char ch : value) {
        native.push_back(static_cast<char8_t>(ch));
    }
    return native;
}

inline std::string pathToUtf8String(const std::filesystem::path& path)
{
    return utf8FromU8(path.u8string());
}

inline std::string pathToGenericUtf8String(const std::filesystem::path& path)
{
    return utf8FromU8(path.generic_u8string());
}

inline std::filesystem::path pathFromUtf8String(std::string_view utf8)
{
    return std::filesystem::path(u8FromUtf8(utf8));
}

} // namespace ya::path_utils
