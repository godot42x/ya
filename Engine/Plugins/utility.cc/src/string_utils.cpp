//
/*
 * @ Author: godot42
 * @ Modified by: @godot42
 * @ Modified by: @godot42
 * @ Modified time: 2025-03-22 00:31:14
 * @ Description:
 */

#include "utility.cc/string_utils.h"
#include <cstddef>
#include <string_view>



namespace ut
{
namespace str
{


std::string replace(std::string_view source, std::string_view from, const std::string_view to)
{
    std::string ret(source);
    size_t      pos = 0;
    while ((pos = ret.find(from, pos)) != std::string::npos) {
        ret.replace(pos, from.size(), to);
        pos += to.size();
    }
    return ret;
}

std::vector<std::string> split(std::string_view source, char delimiter)
{
    std::vector<std::string> ret;
    // "abc def"
    while (true) {
        // printf("source: %s\n", source.data());
        int n = source.find_first_of(delimiter);
        if (n == std::string::npos) {
            ret.emplace_back(source);
            break;
        }
        ret.emplace_back(source.substr(0, n));
        source.remove_prefix(n + 1);
    };
    return ret;
}

bool split(std::string_view source, char sep, std::string &left, std::string_view &right)
{
    int n = source.find_first_of(sep);
    if (n == std::string::npos) {
        left = source;
        return false;
    }
    left  = source.substr(0, n);
    right = source.substr(n + 1, source.size() - n - 1);
    return true;
}

bool left(std::string_view source, char delimiter, std::string_view &left)
{
    size_t n = source.find_first_of(delimiter);
    if (n != std::string::npos) {
        return false;
    }
    left = source.substr(0, n);
    return true;
}


std::string_view trim(std::string_view source)
{
    if (source.empty())
        return "";
    int b = source.find_first_not_of(' ');
    if (b == std::string::npos) {
        return "";
    }
    int  e   = source.find_last_not_of(' ');
    auto ret = source.substr(b, e - b + 1);
    return ret;
}

UTILITY_CC_API std::string toLower(std::string_view source)
{
    std::string ret(source);
    for (char &c : ret) {
        if (c >= 'A' && c <= 'Z') {
            c = c - 'A' + 'a';
        }
    }
    return ret;
}

UTILITY_CC_API std::string toUpper(std::string_view source)
{
    std::string ret(source);
    for (char &c : ret) {
        if (c >= 'a' && c <= 'z') {
            c = c - 'a' + 'A';
        }
    }
    return ret;
}

UTILITY_CC_API std::string concat(std::vector<std::string_view> source, const std::string_view delimiter)
{
    std::string ret;
    ret.reserve(source.size() * 8);
    for (size_t i = 0; i < source.size(); ++i) {
        ret.append(source[i]);
        if (i < source.size() - 1) {
            ret.append(delimiter);
        }
    }
    return ret;
}



} // namespace str

} // namespace ut
