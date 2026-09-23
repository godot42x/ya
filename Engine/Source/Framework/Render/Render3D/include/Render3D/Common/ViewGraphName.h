#pragma once

#include <cstdint>
#include <format>
#include <string>
#include <string_view>

namespace ya
{

[[nodiscard]] inline std::string makeViewGraphName(std::string_view base, uint64_t viewId)
{
    return std::format("{}.view{}", base, viewId);
}

} // namespace ya
