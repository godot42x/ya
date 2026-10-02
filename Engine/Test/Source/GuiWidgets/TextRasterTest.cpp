#include "GUI/Layout/UICanvasLayout.h"
#include "GUI/Widgets/Controls/Text.h"
#include "GUI/Widgets/TextRaster.h"
#include "GUI/Widgets/UIFrameSnapshot.h"
#include "GUI/Widgets/WidgetTree.h"
#include "Render/Resources/FontManager.h"

#include <cmath>

#include <gtest/gtest.h>

namespace ya
{
namespace
{

std::shared_ptr<Font> makeProbeFont(float fontSize, float advancePerChar)
{
    auto font        = std::make_shared<Font>();
    font->fontSize   = fontSize;
    font->lineHeight = fontSize * 1.25f;
    font->ascent     = fontSize;
    font->descent    = fontSize * 0.25f;
    for (uint32_t cp = 32; cp < 127; ++cp) {
        Character ch;
        ch.size       = {static_cast<int>(advancePerChar), static_cast<int>(fontSize)};
        ch.bearing    = {0, 0};
        ch.advance    = {advancePerChar, 0.0f};
        ch.designSize = static_cast<uint32_t>(fontSize);
        font->characters[cp] = ch;
    }
    return font;
}

const UIFrameDrawItem* findText(const UIFrameSnapshot& snapshot)
{
    for (const UIFrameDrawItem& item : snapshot.items) {
        if (item.kind == UIFrameDrawItem::EKind::Text) {
            return &item;
        }
    }
    return nullptr;
}

} // namespace

TEST(TextRasterTest, PlanQuantizesToWholePixelsAndFloorsBitmap)
{
    struct Row
    {
        float    logical;
        float    scale;
        uint32_t raster;
        bool     bBitmap;
    };
    // lround(logical * scale), then bitmap sizes clamp to kMinBitmapRasterPx.
    const Row rows[] = {
        {9.0f, 0.50f, 9, true},  {9.0f, 0.54f, 9, true},  {9.0f, 0.80f, 9, true},
        {9.0f, 1.00f, 9, true},  {9.0f, 1.25f, 11, true}, {9.0f, 2.00f, 18, true},
        {11.0f, 0.50f, 9, true}, {11.0f, 0.54f, 9, true}, {11.0f, 0.80f, 9, true},
        {11.0f, 1.00f, 11, true}, {11.0f, 1.25f, 14, true}, {11.0f, 2.00f, 22, true},
        {13.0f, 0.50f, 9, true}, {13.0f, 0.54f, 9, true}, {13.0f, 0.80f, 10, true},
        {13.0f, 1.00f, 13, true}, {13.0f, 1.25f, 16, true}, {13.0f, 2.00f, 26, true},
        {16.0f, 0.50f, 9, true}, {16.0f, 0.54f, 9, true}, {16.0f, 0.80f, 13, true},
        {16.0f, 1.00f, 16, true}, {16.0f, 1.25f, 20, true}, {16.0f, 2.00f, 32, true},
    };
    for (const Row& row : rows) {
        const FTextRasterPlan plan = planTextRaster(row.logical, {row.scale, row.scale}, 1.0f, {1.0f, 1.0f});
        EXPECT_EQ(plan.rasterPx, row.raster) << row.logical << " @ " << row.scale;
        EXPECT_EQ(plan.bBitmapOneToOne, row.bBitmap);
        EXPECT_FLOAT_EQ(plan.residual.x, 1.0f);
        EXPECT_FLOAT_EQ(plan.residual.y, 1.0f);
        EXPECT_GE(plan.rasterPx, kMinBitmapRasterPx);
    }

    // Zoom and DPI both enter. 16 * 2 * 2 = 64, which is the SDF side of the split.
    const FTextRasterPlan dpiAndZoom = planTextRaster(16.0f, {2.0f, 2.0f}, 2.0f, {1.0f, 1.0f});
    EXPECT_EQ(dpiAndZoom.rasterPx, 64u);
    EXPECT_FALSE(dpiAndZoom.bBitmapOneToOne);
    EXPECT_FLOAT_EQ(dpiAndZoom.residual.x, 1.0f);

    // Em size is the Y axis, so a non-uniform scale does not stretch glyphs.
    const FTextRasterPlan nonUniform = planTextRaster(16.0f, {4.0f, 0.5f}, 1.0f, {1.0f, 1.0f});
    EXPECT_EQ(nonUniform.rasterPx, kMinBitmapRasterPx);
    EXPECT_FLOAT_EQ(nonUniform.deviceScale.x, 4.0f);
    EXPECT_FLOAT_EQ(nonUniform.deviceScale.y, 0.5f);
    EXPECT_FLOAT_EQ(nonUniform.residual.x, 1.0f);

    const FTextRasterPlan renderScale = planTextRaster(16.0f, {1.0f, 1.0f}, 1.0f, {2.0f, 2.0f});
    EXPECT_EQ(renderScale.rasterPx, 32u);
    EXPECT_TRUE(renderScale.bBitmapOneToOne);

    const FTextRasterPlan sdf = planTextRaster(49.0f, {1.0f, 1.0f}, 1.0f, {1.0f, 1.0f});
    EXPECT_EQ(sdf.rasterPx, 49u);
    EXPECT_FALSE(sdf.bBitmapOneToOne);
    EXPECT_FLOAT_EQ(sdf.residual.x, 1.0f);
}

TEST(TextRasterTest, DesignerZoomRerasterizesAndLayoutStaysLogical)
{
    const FName family("RasterProbe");
    FontManager::get()->registerFont(family, 16, makeProbeFont(16.0f, 8.0f));
    FontManager::get()->registerFont(family, 32, makeProbeFont(32.0f, 16.0f));

    WidgetTree tree({.width = 400, .height = 300});
    auto       text = std::make_shared<UIText>("Label");
    text->setText("AB");
    text->setFontSize(16);
    text->setFontFamily(family.toString());
    FCanvasSlotArgs slot;
    slot.widthSizeMode  = EWidgetSizeMode::Auto;
    slot.heightSizeMode = EWidgetSizeMode::Auto;
    tree.attach(*tree.getLayer(WidgetTree::ELayer::Content), text, slot);

    const UIFrameSnapshot atOne = tree.buildSnapshot(UIFrameBuildContext{});
    const glm::vec2       layoutAtOne = text->_layoutRect.extent;
    const UIFrameDrawItem* drawnAtOne = findText(atOne);
    ASSERT_NE(drawnAtOne, nullptr);
    EXPECT_FLOAT_EQ(layoutAtOne.x, 16.0f);
    EXPECT_EQ(drawnAtOne->font->getFontSize(), 16.0f);
    EXPECT_FLOAT_EQ(drawnAtOne->textScale.x, 1.0f);
    EXPECT_FLOAT_EQ(drawnAtOne->textScale.y, 1.0f);

    // Designer preview: tree dpi stays 1, canvas zoom arrives as uiScale.
    UIFrameBuildContext zoomed{};
    zoomed.uiScale = {2.0f, 2.0f};
    const UIFrameSnapshot atTwo = tree.buildSnapshot(zoomed);
    const UIFrameDrawItem* drawnAtTwo = findText(atTwo);
    ASSERT_NE(drawnAtTwo, nullptr);
    EXPECT_FLOAT_EQ(text->_layoutRect.extent.x, layoutAtOne.x);
    EXPECT_FLOAT_EQ(text->_layoutRect.extent.y, layoutAtOne.y);
    EXPECT_EQ(drawnAtTwo->font.get(), FontManager::get()->getFont(family, 32).get());
    EXPECT_FLOAT_EQ(drawnAtTwo->textScale.x, 1.0f);
    EXPECT_FLOAT_EQ(drawnAtTwo->textScale.y, 1.0f);
    EXPECT_FLOAT_EQ(drawnAtTwo->size.x, 32.0f);
    EXPECT_NEAR(drawnAtTwo->pos.x, std::round(drawnAtTwo->pos.x), 1e-4f);
    EXPECT_NEAR(drawnAtTwo->pos.y, std::round(drawnAtTwo->pos.y), 1e-4f);
}

TEST(TextRasterTest, TreeDpiRerasterizesWithoutChangingLayout)
{
    const FName family("RasterProbeDpi");
    FontManager::get()->registerFont(family, 16, makeProbeFont(16.0f, 8.0f));
    FontManager::get()->registerFont(family, 32, makeProbeFont(32.0f, 16.0f));

    WidgetTree tree({.width = 400, .height = 300});
    tree.setDpiScale(1.0f);
    auto text = std::make_shared<UIText>("Label");
    text->setText("AB");
    text->setFontSize(16);
    text->setFontFamily(family.toString());
    FCanvasSlotArgs slot;
    slot.widthSizeMode  = EWidgetSizeMode::Auto;
    slot.heightSizeMode = EWidgetSizeMode::Auto;
    tree.attach(*tree.getLayer(WidgetTree::ELayer::Content), text, slot);

    const UIFrameSnapshot atOne = tree.buildSnapshot(UIFrameBuildContext{});
    const glm::vec2       layoutAtOne = text->_layoutRect.extent;

    tree.setDpiScale(2.0f);
    const UIFrameSnapshot atTwo = tree.buildSnapshot(UIFrameBuildContext{});
    const UIFrameDrawItem* drawn = findText(atTwo);
    ASSERT_NE(drawn, nullptr);
    EXPECT_FLOAT_EQ(text->_layoutRect.extent.x, layoutAtOne.x);
    EXPECT_FLOAT_EQ(text->_layoutRect.extent.y, layoutAtOne.y);
    EXPECT_EQ(drawn->font.get(), FontManager::get()->getFont(family, 32).get());
    EXPECT_FLOAT_EQ(drawn->textScale.x, 1.0f);
    EXPECT_FLOAT_EQ(drawn->size.x, 32.0f);
    (void)atOne;
}

TEST(TextRasterTest, BitmapFloorClampsRasterAndKeepsLogicalLayout)
{
    const FName family("RasterProbeFloor");
    FontManager::get()->registerFont(family, 13, makeProbeFont(13.0f, 6.0f));
    FontManager::get()->registerFont(family, 9, makeProbeFont(9.0f, 5.0f));

    WidgetTree tree({.width = 400, .height = 300});
    auto       text = std::make_shared<UIText>("Label");
    text->setText("AB");
    text->setFontSize(13);
    text->setFontFamily(family.toString());
    FCanvasSlotArgs slot;
    slot.widthSizeMode  = EWidgetSizeMode::Auto;
    slot.heightSizeMode = EWidgetSizeMode::Auto;
    tree.attach(*tree.getLayer(WidgetTree::ELayer::Content), text, slot);

    UIFrameBuildContext ctx{};
    ctx.uiScale = {0.2f, 0.2f};
    const UIFrameSnapshot snapshot = tree.buildSnapshot(ctx);
    const UIFrameDrawItem* drawn = findText(snapshot);
    ASSERT_NE(drawn, nullptr);
    EXPECT_FLOAT_EQ(text->_layoutRect.extent.x, 12.0f);
    EXPECT_EQ(drawn->font.get(), FontManager::get()->getFont(family, kMinBitmapRasterPx).get());
    EXPECT_FLOAT_EQ(drawn->textScale.x, 1.0f);
    EXPECT_FLOAT_EQ(drawn->textScale.y, 1.0f);
    EXPECT_FLOAT_EQ(drawn->size.x, 10.0f);
}

} // namespace ya
