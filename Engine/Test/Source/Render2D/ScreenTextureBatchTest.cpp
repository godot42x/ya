#include "Render2D/ScreenDrawList.h"
#include "Render2D/TextureTableBatch.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <cstdint>
#include <vector>

namespace ya
{

namespace
{

Texture* fakeTexture(int id)
{
    return reinterpret_cast<Texture*>(static_cast<uintptr_t>(id));
}

TextureTableKey keyFor(int id)
{
    return TextureTableKey{.identity = reinterpret_cast<const void*>(static_cast<uintptr_t>(id))};
}

struct Mesh
{
    std::vector<uint32_t>        indices;
    std::vector<uint32_t>        slots;
    std::vector<TextureTableKey> keys;
    std::vector<ScreenDrawCommandSpan> commands;
};

void addQuad(Mesh& mesh, uint32_t slot)
{
    const uint32_t base = static_cast<uint32_t>(mesh.slots.size());
    mesh.slots.insert(mesh.slots.end(), 4, slot);
    mesh.indices.insert(mesh.indices.end(), {base, base + 1, base + 3, base, base + 3, base + 2});
}

ScreenDrawRemap plan(const Mesh& mesh, uint32_t maxVertices = 100000, uint32_t capacity = TextureTableCursor::kDefaultCapacity)
{
    return planScreenDrawRemap(
        mesh.indices,
        static_cast<uint32_t>(mesh.slots.size()),
        [&](uint32_t vertex) { return mesh.slots[vertex]; },
        mesh.keys,
        keyFor(0),
        mesh.commands,
        maxVertices,
        maxVertices * 3,
        capacity);
}

Mesh quads(const std::vector<int>& textures)
{
    Mesh mesh;
    int maxSlot = 0;
    for (int texture : textures) {
        maxSlot = std::max(maxSlot, texture);
    }
    mesh.keys.resize(static_cast<size_t>(maxSlot) + 1u, keyFor(0));
    for (int texture : textures) {
        mesh.keys[static_cast<size_t>(texture)] = keyFor(texture);
    }
    for (int texture : textures) {
        addQuad(mesh, static_cast<uint32_t>(texture));
    }
    mesh.commands.push_back(ScreenDrawCommandSpan{
        .firstIndex = 0,
        .indexCount = static_cast<uint32_t>(mesh.indices.size()),
        .bClipped   = false,
    });
    return mesh;
}

} // namespace

TEST(ScreenTextureBatchTest, SixteenSlotTableCutsBeforeTheNextTexture)
{
    // Captured from the pre-migration recorder: slot 0 is white, so 15 distinct
    // textures fill the table and the 16th starts a new batch, remapped at slot 1.
    std::vector<int> ids;
    for (int id = 1; id <= 20; ++id) {
        ids.push_back(id);
    }
    const ScreenDrawRemap remap = plan(quads(ids));
    ASSERT_EQ(remap.batches.size(), 2u);
    EXPECT_EQ(remap.batches[0].vertexCount, 60u);
    EXPECT_EQ(remap.batches[0].indexCount, 90u);
    EXPECT_EQ(remap.batches[0].catalogSlots.size(), 16u);
    EXPECT_EQ(remap.batches[1].vertexCount, 20u);
    EXPECT_EQ(remap.batches[1].catalogSlots.size(), 6u);
    EXPECT_EQ(remap.gpuSlots[0], 1u);
    EXPECT_EQ(remap.gpuSlots[14 * 4], 15u);
    EXPECT_EQ(remap.gpuSlots[15 * 4], 1u);
    EXPECT_EQ(remap.gpuSlots[19 * 4], 5u);
}

TEST(ScreenTextureBatchTest, RepeatedTextureStaysOneBatch)
{
    const ScreenDrawRemap remap = plan(quads(std::vector<int>(20, 4)));
    ASSERT_EQ(remap.batches.size(), 1u);
    EXPECT_EQ(remap.batches[0].catalogSlots.size(), 2u);
    EXPECT_EQ(remap.gpuSlots.front(), 1u);
    EXPECT_EQ(remap.gpuSlots.back(), 1u);
}

TEST(ScreenTextureBatchTest, InterleavedTexturesThatFitStayOneBatch)
{
    const ScreenDrawRemap remap = plan(quads({1, 2, 1}));
    ASSERT_EQ(remap.batches.size(), 1u);
    EXPECT_EQ(remap.gpuSlots[0], 1u);
    EXPECT_EQ(remap.gpuSlots[4], 2u);
    EXPECT_EQ(remap.gpuSlots[8], 1u);
}

TEST(ScreenTextureBatchTest, QuadSharesVerticesInsideTheBatch)
{
    const ScreenDrawRemap remap = plan(quads({3}), 4);
    ASSERT_EQ(remap.batches.size(), 1u);
    EXPECT_EQ(remap.batches[0].vertexCount, 4u);
    EXPECT_EQ(remap.batches[0].indexCount, 6u);
}

TEST(ScreenTextureBatchTest, CapacityFlushKeepsTheTextureSlot)
{
    const ScreenDrawRemap remap = plan(quads({7, 7}), 4);
    ASSERT_EQ(remap.batches.size(), 2u);
    EXPECT_EQ(remap.batches[0].vertexCount, 4u);
    EXPECT_EQ(remap.batches[1].vertexCount, 4u);
    EXPECT_EQ(remap.gpuSlots[0], 1u);
    EXPECT_EQ(remap.gpuSlots[4], 1u);
    EXPECT_EQ(remap.batches[0].catalogSlots, remap.batches[1].catalogSlots);
}

TEST(ScreenTextureBatchTest, ClipFlushDoesNotResetTheTextureTable)
{
    Mesh mesh = quads({1, 2});
    mesh.commands.clear();
    mesh.commands.push_back(ScreenDrawCommandSpan{.firstIndex = 0, .indexCount = 6, .bClipped = false});
    mesh.commands.push_back(ScreenDrawCommandSpan{
        .firstIndex = 6,
        .indexCount = 6,
        .bClipped   = true,
        .clip       = Rect2D{.pos = {1.0f, 2.0f}, .extent = {3.0f, 4.0f}},
    });
    const ScreenDrawRemap remap = plan(mesh);
    ASSERT_EQ(remap.batches.size(), 2u);
    EXPECT_FALSE(remap.batches[0].bClipped);
    EXPECT_TRUE(remap.batches[1].bClipped);
    EXPECT_EQ(remap.batches[1].clip.extent.x, 3.0f);
    EXPECT_EQ(remap.batches[0].catalogSlots.size(), 2u);
    EXPECT_EQ(remap.batches[1].catalogSlots.size(), 3u);
    EXPECT_EQ(remap.gpuSlots[0], 1u);
    EXPECT_EQ(remap.gpuSlots[4], 2u);
}

TEST(ScreenTextureBatchTest, UnmappedWhiteStillCutsWhenTheTableIsFull)
{
    // The old recorder flushed before it knew the unmapped slot was white.
    std::vector<int> ids;
    for (int id = 1; id <= 15; ++id) {
        ids.push_back(id);
    }
    ids.push_back(0);
    ids.push_back(16);
    const ScreenDrawRemap remap = plan(quads(ids));
    ASSERT_EQ(remap.batches.size(), 2u);
    EXPECT_EQ(remap.batches[0].vertexCount, 60u);
    EXPECT_EQ(remap.batches[1].vertexCount, 8u);
    EXPECT_EQ(remap.gpuSlots[15 * 4], 0u);
    EXPECT_EQ(remap.gpuSlots[16 * 4], 1u);
    EXPECT_EQ(remap.batches[1].catalogSlots.size(), 2u);
}

TEST(ScreenDrawListTest, ManyTexturesDoNotSplitCommands)
{
    ScreenDrawList list;
    for (int id = 1; id <= 20; ++id) {
        list.makeSprite(glm::vec3(static_cast<float>(id), 0.0f, 0.0f),
                        glm::vec2(1.0f, 1.0f),
                        fakeTexture(id));
    }
    list.seal();
    ASSERT_EQ(list.commands.size(), 1u);
    EXPECT_EQ(list.commands[0].vertexCount, 80u);
    EXPECT_EQ(list.vertices.front().textureSlot, 1u);
    EXPECT_EQ(list.textures.front().get(), nullptr);
    EXPECT_EQ(list.textures.size(), 21u);
}

TEST(ScreenDrawListTest, NullTextureIsSlotZero)
{
    ScreenDrawList list;
    list.makeSprite(glm::vec3(0.0f, 0.0f, 0.0f), glm::vec2(1.0f));
    list.makeSprite(glm::vec3(2.0f, 0.0f, 0.0f), glm::vec2(1.0f), fakeTexture(4));
    list.makeSprite(glm::vec3(4.0f, 0.0f, 0.0f), glm::vec2(1.0f));
    list.seal();
    EXPECT_EQ(list.vertices[0].textureSlot, 0u);
    EXPECT_EQ(list.vertices[4].textureSlot, 1u);
    EXPECT_EQ(list.vertices[8].textureSlot, 0u);
    ASSERT_EQ(list.commands.size(), 1u);
}

} // namespace ya
