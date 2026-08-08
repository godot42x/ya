#include "Foundation/RHI/Core/Texture.h"

#include <gtest/gtest.h>

namespace ya
{

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

} // namespace ya
