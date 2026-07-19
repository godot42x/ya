#include "Resource/Texture/AssetTextureInternal.h"
#include "Render/Core/Texture.h"

#include <gtest/gtest.h>

namespace ya::asset_manager_texture_detail
{

TEST(AssetTextureMipTest, GeneratesCompleteU8BoxFilteredMipChain)
{
    AssetManager::TextureMemoryBlock texture{
        .width       = 4,
        .height      = 4,
        .channels    = 1,
        .payloadType = AssetManager::ETexturePayloadType::U8,
        .bytes       = {0, 1, 2, 3,
                        4, 5, 6, 7,
                        8, 9, 10, 11,
                        12, 13, 14, 15},
    };

    generateU8MipChain(texture);

    EXPECT_EQ(texture.mipLevels, 3u);
    EXPECT_EQ(texture.bytes.size(), 21u);
    EXPECT_EQ(std::vector<uint8_t>(texture.bytes.begin() + 16, texture.bytes.begin() + 20),
              (std::vector<uint8_t>{3, 5, 11, 13}));
    EXPECT_EQ(texture.bytes.back(), 8u);
}

TEST(AssetTextureMipTest, ReportsBaseLevelSizeForMipChain)
{
    std::vector<uint8_t> mipChain(4 * 4 * 4 + 2 * 2 * 4 + 1 * 1 * 4);
    TextureMemoryView face{
        .width     = 4,
        .height    = 4,
        .channels  = 4,
        .mipLevels = 3,
        .format    = EFormat::R8G8B8A8_UNORM,
        .data      = mipChain.data(),
        .dataSize  = mipChain.size(),
    };

    EXPECT_TRUE(face.isValid());
    EXPECT_EQ(face.baseLevelDataSize(), 4u * 4u * 4u);
    EXPECT_LT(face.baseLevelDataSize(), face.dataSize);
}

} // namespace ya::asset_manager_texture_detail
