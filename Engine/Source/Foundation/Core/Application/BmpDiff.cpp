#include "Core/Application/BmpDiff.h"

#include "Core/Log.h"

#include <algorithm>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <vector>

namespace ya
{

namespace
{

bool loadBmp(const std::string& path,
             uint32_t&          width,
             uint32_t&          height,
             std::vector<uint8_t>& topDownBgr,
             std::string&       error)
{
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        error = "cannot open " + path;
        return false;
    }
    std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(file)),
                               std::istreambuf_iterator<char>());
    if (bytes.size() < 54 || bytes[0] != 'B' || bytes[1] != 'M') {
        error = "not a BMP: " + path;
        return false;
    }

    const auto readU32 = [&](size_t offset) -> uint32_t {
        return static_cast<uint32_t>(bytes[offset]) |
               (static_cast<uint32_t>(bytes[offset + 1]) << 8) |
               (static_cast<uint32_t>(bytes[offset + 2]) << 16) |
               (static_cast<uint32_t>(bytes[offset + 3]) << 24);
    };
    const uint32_t dataOffset = readU32(10);
    width                     = readU32(18);
    height                    = readU32(22);
    const uint16_t bpp        = static_cast<uint16_t>(bytes[28]) |
                                (static_cast<uint16_t>(bytes[29]) << 8);
    if (bpp != 24) {
        error = "only 24-bit BMP supported: " + path;
        return false;
    }
    const uint32_t rowStride = ((width * 3 + 3) / 4) * 4;
    if (dataOffset + rowStride * height > bytes.size()) {
        error = "truncated BMP: " + path;
        return false;
    }

    topDownBgr.resize(static_cast<size_t>(width) * height * 3);
    for (uint32_t y = 0; y < height; ++y) {
        const uint8_t* src = bytes.data() + dataOffset + (height - 1 - y) * rowStride;
        uint8_t*       dst = topDownBgr.data() + static_cast<size_t>(y) * width * 3;
        for (uint32_t x = 0; x < width * 3; ++x) {
            dst[x] = src[x];
        }
    }
    return true;
}

bool saveBmp(const std::string& path,
             uint32_t          width,
             uint32_t          height,
             const std::vector<uint8_t>& topDownBgr)
{
    const uint32_t rowStride = ((width * 3 + 3) / 4) * 4;
    const uint32_t imageSize = rowStride * height;
    const uint32_t fileSize  = 54 + imageSize;

    std::ofstream file(path, std::ios::binary);
    if (!file) {
        return false;
    }
    const auto putU16 = [&](uint16_t v) {
        file.put(static_cast<char>(v & 0xFF));
        file.put(static_cast<char>((v >> 8) & 0xFF));
    };
    const auto putU32 = [&](uint32_t v) {
        file.put(static_cast<char>(v & 0xFF));
        file.put(static_cast<char>((v >> 8) & 0xFF));
        file.put(static_cast<char>((v >> 16) & 0xFF));
        file.put(static_cast<char>((v >> 24) & 0xFF));
    };
    file.put('B');
    file.put('M');
    putU32(fileSize);
    putU32(0);
    putU32(54);
    putU32(40);
    putU32(width);
    putU32(height);
    putU16(1);
    putU16(24);
    putU32(0);
    putU32(imageSize);
    putU32(2835);
    putU32(2835);
    putU32(0);
    putU32(0);

    std::vector<uint8_t> row(rowStride, 0);
    for (uint32_t y = 0; y < height; ++y) {
        const uint8_t* src = topDownBgr.data() + static_cast<size_t>(height - 1 - y) * width * 3;
        std::copy(src, src + width * 3, row.begin());
        file.write(reinterpret_cast<const char*>(row.data()), rowStride);
    }
    return true;
}

} // namespace

BmpDiffResult diffBmpFiles(const std::string& baseline,
                           const std::string& actual,
                           const std::string& diffOut,
                           uint8_t           threshold,
                           float             maxDiffRatio)
{
    BmpDiffResult result;
    uint32_t      bw = 0, bh = 0, aw = 0, ah = 0;
    std::vector<uint8_t> base, act;
    std::string error;
    if (!loadBmp(baseline, bw, bh, base, error) || !loadBmp(actual, aw, ah, act, error)) {
        YA_CORE_ERROR("BmpDiff: {}", error);
        return result;
    }
    if (bw != aw || bh != ah) {
        YA_CORE_ERROR("BmpDiff: size mismatch {}x{} vs {}x{}", bw, bh, aw, ah);
        return result;
    }

    result.totalPixels = static_cast<uint64_t>(bw) * bh;
    std::vector<uint8_t> diff(base.size(), 0);
    for (size_t i = 0; i < base.size(); i += 3) {
        bool differing = false;
        for (size_t c = 0; c < 3; ++c) {
            const int delta = static_cast<int>(act[i + c]) - static_cast<int>(base[i + c]);
            if (std::abs(delta) > threshold) {
                differing = true;
                break;
            }
        }
        if (differing) {
            ++result.differingPixels;
            diff[i + 0] = 0;     // B
            diff[i + 1] = 0;     // G
            diff[i + 2] = 255;   // R -> red highlight
        }
        else {
            for (size_t c = 0; c < 3; ++c) {
                diff[i + c] = static_cast<uint8_t>(base[i + c] / 4);
            }
        }
    }

    result.diffRatio = result.totalPixels == 0
                           ? 0.0f
                           : static_cast<float>(result.differingPixels) /
                                 static_cast<float>(result.totalPixels);
    result.bPass = result.diffRatio <= maxDiffRatio;
    if (!diffOut.empty() && !saveBmp(diffOut, bw, bh, diff)) {
        YA_CORE_ERROR("BmpDiff: cannot write diff image '{}'", diffOut);
    }
    return result;
}

} // namespace ya
