#include "GameEditor/UI/Tabs/EditorDebugCatalogView.h"

#include <gtest/gtest.h>

namespace ya
{

TEST(EditorDebugCatalogViewTest, PreviewSlotIndexOffsetsByGroupAndFace)
{
    EXPECT_EQ(debugGroupPreviewSlotIndex(10, 6, 2, 3), 10u + 2u * 6u + 3u);
}

TEST(EditorDebugCatalogViewTest, FiltersStandaloneSlotsAndGroupsByCategory)
{
    RenderViewportDebugCatalog catalog;
    catalog.categories = {{"gbuffer", "GBuffer"}, {"lighting", "Lighting"}};
    catalog.slots      = {
        {.label = "Albedo", .categoryIndex = 0},
        {.label = "Normal", .categoryIndex = 0},
        {.label = "Shadow", .categoryIndex = 1},
        {.label = "LUT", .categoryIndex = 1},
    };
    catalog.groups = {{
        .label         = "Cube",
        .categoryIndex = 1,
        .beginIndex    = 2,
        .slotCount     = 1,
        .groupSize     = 1,
    }};

    const auto lightingGroups = debugGroupIndicesForCategory(catalog, 1);
    ASSERT_EQ(lightingGroups.size(), 1u);
    EXPECT_EQ(lightingGroups.front(), 0);

    const auto gbufferSlots = debugStandaloneSlotIndices(catalog, 0);
    ASSERT_EQ(gbufferSlots.size(), 2u);
    EXPECT_EQ(gbufferSlots[0], 0);
    EXPECT_EQ(gbufferSlots[1], 1);

    const auto lightingSlots = debugStandaloneSlotIndices(catalog, 1);
    ASSERT_EQ(lightingSlots.size(), 1u);
    EXPECT_EQ(lightingSlots.front(), 3);

    const auto allSlots = debugStandaloneSlotIndices(catalog, -1);
    ASSERT_EQ(allSlots.size(), 3u);
}

} // namespace ya
