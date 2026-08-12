// BmpDiff regression (shared app foundation, pure CPU). Exercises the golden
// image comparison the GUI scenario harness uses for pixel assertions.

#include "Core/Application/BmpDiff.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace ya
{
namespace
{

void writeBmp24(const std::string& path,
                uint32_t          width,
                uint32_t          height,
                const std::vector<uint8_t>& topDownBgr)
{
    const uint32_t rowStride  = ((width * 3 + 3) / 4) * 4;
    const uint32_t imageSize  = rowStride * height;
    const uint32_t fileSize   = 54 + imageSize;

    std::ofstream file(path, std::ios::binary);
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
}

std::string tempPath(const char* name)
{
    return (std::filesystem::temp_directory_path() / name).string();
}

} // namespace

TEST(BmpDiffTest, IdenticalImagesPass)
{
    // 2x2, every pixel BGR = (10, 20, 30).
    std::vector<uint8_t> pixels(2 * 2 * 3);
    for (size_t i = 0; i < pixels.size(); i += 3) {
        pixels[i]     = 10;
        pixels[i + 1] = 20;
        pixels[i + 2] = 30;
    }
    const std::string base = tempPath("bmpdiff_base.bmp");
    const std::string act  = tempPath("bmpdiff_act.bmp");
    const std::string diff = tempPath("bmpdiff_diff.bmp");
    writeBmp24(base, 2, 2, pixels);
    writeBmp24(act, 2, 2, pixels);

    const BmpDiffResult result = diffBmpFiles(base, act, diff, 16, 0.0f);
    EXPECT_TRUE(result.bPass);
    EXPECT_EQ(result.differingPixels, 0u);
    EXPECT_FLOAT_EQ(result.diffRatio, 0.0f);
    EXPECT_TRUE(std::filesystem::exists(diff));
}

TEST(BmpDiffTest, SingleChangedPixelFailsAndWritesHighlight)
{
    std::vector<uint8_t> base(2 * 2 * 3);
    for (size_t i = 0; i < base.size(); i += 3) {
        base[i]     = 10;
        base[i + 1] = 20;
        base[i + 2] = 30;
    }
    std::vector<uint8_t> act = base;
    act[0]                   = 200; // B channel of pixel 0 jumps well past threshold

    const std::string basePath = tempPath("bmpdiff_base2.bmp");
    const std::string actPath  = tempPath("bmpdiff_act2.bmp");
    const std::string diffPath = tempPath("bmpdiff_diff2.bmp");
    writeBmp24(basePath, 2, 2, base);
    writeBmp24(actPath, 2, 2, act);

    const BmpDiffResult result = diffBmpFiles(basePath, actPath, diffPath, 16, 0.0f);
    EXPECT_FALSE(result.bPass);
    EXPECT_EQ(result.differingPixels, 1u);
    EXPECT_FLOAT_EQ(result.diffRatio, 0.25f);
    EXPECT_TRUE(std::filesystem::exists(diffPath));
}

} // namespace ya
