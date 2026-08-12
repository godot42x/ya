#pragma once

// Golden-image comparison for the automation harness. Loads two 24-bit BMPs
// (the format the GUI capture path already writes), compares them per-channel
// against a threshold, and writes a difference image (red = differing pixel,
// darkened = matching). Pure CPU / no RHI, so scenario tests can assert pixels
// without a GPU.

#include "Core/Api.h"

#include <cstdint>
#include <string>

namespace ya
{

struct BmpDiffResult
{
    bool     bPass           = false;
    uint64_t differingPixels = 0;
    uint64_t totalPixels     = 0;
    float    diffRatio       = 0.0f;
};

/// Compare baseline and actual (24-bit BMPs). threshold is the per-channel
/// absolute tolerance; maxDiffRatio is the allowed fraction of differing
/// pixels (0 = any difference fails). Always writes diffOut on success.
YA_CORE_API BmpDiffResult diffBmpFiles(const std::string& baseline,
                                       const std::string& actual,
                                       const std::string& diffOut,
                                       uint8_t threshold    = 16,
                                       float   maxDiffRatio = 0.0f);

} // namespace ya
