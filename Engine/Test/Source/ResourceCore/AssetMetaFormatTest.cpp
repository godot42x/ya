#include "Resource/Core/Meta/AssetMeta.h"

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <sstream>

namespace ya
{

TEST(AssetMetaFormatTest, SaveUsesCompactLeavesAndStillReadsDump4)
{
    AssetMeta meta;
    meta.type                 = "texture";
    meta.properties           = nlohmann::json::object();
    meta.properties["colorSpace"] = "srgb";
    meta.properties["lods"]       = nlohmann::json::array({0, 1, 2, 3});

    const std::filesystem::path path =
        std::filesystem::temp_directory_path() / "ya-asset-meta-format-test.ya-meta.json";
    meta.saveToFile(path.string());

    std::ifstream     input(path);
    std::stringstream buffer;
    buffer << input.rdbuf();
    const std::string text = buffer.str();

    const std::string expected =
        "{\n"
        "  \"properties\": {\n"
        "    \"colorSpace\": \"srgb\",\n"
        "    \"lods\": [0, 1, 2, 3]\n"
        "  },\n"
        "  \"type\": \"texture\"\n"
        "}";
    EXPECT_EQ(text, expected);

    const AssetMeta loaded = AssetMeta::loadFromFile(path.string());
    EXPECT_EQ(loaded, meta);

    // Files already on disk use dump(4). They still parse; the next save
    // rewrites them in the compact-leaf layout. This test does not touch
    // any meta file in the repository.
    const std::string legacy = meta.toJson().dump(4, ' ', false);
    EXPECT_NE(legacy, expected);
    {
        std::ofstream output(path);
        output << legacy;
    }
    const AssetMeta fromLegacy = AssetMeta::loadFromFile(path.string());
    EXPECT_EQ(fromLegacy, meta);

    fromLegacy.saveToFile(path.string());
    std::ifstream     rewrittenInput(path);
    std::stringstream rewritten;
    rewritten << rewrittenInput.rdbuf();
    EXPECT_EQ(rewritten.str(), expected);

    std::filesystem::remove(path);
}

} // namespace ya
