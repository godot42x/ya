#pragma once

// String form of EPresentMode. The labels are what configs and
// bench_summary.json store; they are not the backend enum names.

#include "RHI/RenderDefines.h"

#include <algorithm>
#include <array>
#include <string_view>
#include <vector>

namespace ya
{

[[nodiscard]] inline const char* toPresentModeString(EPresentMode::T mode)
{
    switch (mode) {
    case EPresentMode::Immediate:
        return "immediate";
    case EPresentMode::Mailbox:
        return "mailbox";
    case EPresentMode::FIFO:
        return "fifo";
    case EPresentMode::FIFO_Relaxed:
        return "fifo_relaxed";
    }
    return "unknown";
}

[[nodiscard]] inline bool tryParsePresentMode(std::string_view text, EPresentMode::T& outMode)
{
    for (const EPresentMode::T mode : {EPresentMode::Immediate, EPresentMode::Mailbox, EPresentMode::FIFO, EPresentMode::FIFO_Relaxed}) {
        if (text == toPresentModeString(mode)) {
            outMode = mode;
            return true;
        }
    }
    return false;
}

/// First entry of `preference` the surface offers, else FIFO.
[[nodiscard]] inline EPresentMode::T choosePreferredPresentMode(EPresentMode::T                     preference,
                                                                const std::vector<EPresentMode::T>& available)
{
    const std::array<EPresentMode::T, 5> order{
        preference,
        EPresentMode::Immediate,
        EPresentMode::Mailbox,
        EPresentMode::FIFO_Relaxed,
        EPresentMode::FIFO,
    };
    for (const EPresentMode::T mode : order) {
        if (std::find(available.begin(), available.end(), mode) != available.end()) {
            return mode;
        }
    }
    return EPresentMode::FIFO;
}

} // namespace ya
