#include "Sprite2DWorld.slang.h"

#include <gtest/gtest.h>

#include <cstddef>
#include <cstring>

namespace ya
{

TEST(SpriteInstanceLayout, VertexInputFieldsMatchTheRecord)
{
    using slang_types::Sprite2DWorld::SpriteInstance;
    using slang_types::Sprite2DWorld::SpriteInstanceFields;
    using slang_types::Sprite2DWorld::VertexInputScalar;

    static_assert(sizeof(SpriteInstance) == 72);
    static_assert(offsetof(SpriteInstance, worldCenter) == 0);
    static_assert(offsetof(SpriteInstance, textureIndex) == 12);
    static_assert(offsetof(SpriteInstance, axisX) == 16);
    static_assert(offsetof(SpriteInstance, axisY) == 28);
    static_assert(offsetof(SpriteInstance, uvRect) == 40);
    static_assert(offsetof(SpriteInstance, tint) == 56);

    ASSERT_EQ(sizeof(SpriteInstanceFields) / sizeof(SpriteInstanceFields[0]), 6u);

    const auto expect = [](const slang_types::Sprite2DWorld::VertexInputField& field,
                           const char* name,
                           uint32_t location,
                           uint32_t offset,
                           uint32_t components,
                           VertexInputScalar scalar)
    {
        EXPECT_STREQ(field.name, name);
        EXPECT_EQ(field.location, location);
        EXPECT_EQ(field.offset, offset);
        EXPECT_EQ(field.components, components);
        EXPECT_EQ(field.scalar, scalar);
    };

    expect(SpriteInstanceFields[0], "worldCenter", 3, offsetof(SpriteInstance, worldCenter), 3, VertexInputScalar::Float32);
    expect(SpriteInstanceFields[1], "textureIndex", 4, offsetof(SpriteInstance, textureIndex), 1, VertexInputScalar::Uint32);
    expect(SpriteInstanceFields[2], "axisX", 5, offsetof(SpriteInstance, axisX), 3, VertexInputScalar::Float32);
    expect(SpriteInstanceFields[3], "axisY", 6, offsetof(SpriteInstance, axisY), 3, VertexInputScalar::Float32);
    expect(SpriteInstanceFields[4], "uvRect", 7, offsetof(SpriteInstance, uvRect), 4, VertexInputScalar::Float32);
    expect(SpriteInstanceFields[5], "tint", 8, offsetof(SpriteInstance, tint), 4, VertexInputScalar::Float32);
}

} // namespace ya
