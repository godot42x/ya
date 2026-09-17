#pragma once

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <string>
#include <string_view>

namespace ya
{

enum class EStringMatchCase : uint8_t
{
    Ignore,
    Sensitive,
};

/// Substring match used by SearchCombo / TreeView filter (and any future
/// GUI string filter). Default is case-insensitive; opt in to Sensitive.
[[nodiscard]] inline bool stringContains(std::string_view haystack,
                                         std::string_view needle,
                                         EStringMatchCase mode = EStringMatchCase::Ignore)
{
    if (needle.empty()) {
        return true;
    }
    if (needle.size() > haystack.size()) {
        return false;
    }
    if (mode == EStringMatchCase::Sensitive) {
        return haystack.find(needle) != std::string_view::npos;
    }
    const auto fold = [](unsigned char c) { return static_cast<char>(std::tolower(c)); };
    const auto it   = std::search(haystack.begin(),
                                  haystack.end(),
                                  needle.begin(),
                                  needle.end(),
                                  [&](char a, char b) { return fold(static_cast<unsigned char>(a)) ==
                                                             fold(static_cast<unsigned char>(b)); });
    return it != haystack.end();
}

} // namespace ya
