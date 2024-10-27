/**
 *  Author: @godot42
 *  Create Time: 2024-07-21 03:46:32
 *  Modified by: @godot42
 *  Modified time: 2024-07-21 03:57:34
 *  Description:
 */



#pragma once

#include <string>
#include <vector>
namespace utils {


inline std::vector<std::string> string_split(std::string_view source, char delimiter = ' ')
{
    std::vector<std::string> ret;
    int                      nth = 0;
    // "abc def"
    while ((nth = source.find(delimiter)) != std::string::npos) {
        ret.emplace_back(source.substr(0, nth - 1));
        source.remove_prefix(nth + 1);
    }
    return ret;
}

} // namespace utils