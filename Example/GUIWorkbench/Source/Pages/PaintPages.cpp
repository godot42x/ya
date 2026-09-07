#include "../WorkbenchDemoPages.h"
#include "DemoPageCommon.h"

#include "GUI/Declarative/Build.h"
#include "GUI/Widgets/Brush.h"
#include "GUI/Widgets/Controls/Image.h"

#include <format>

namespace guiworkbench
{

void buildBrushDemo(ya::WidgetTree& tree, ya::UIElement& parent, FDemoState& state,
                    const std::function<void(const std::string&)>& log)
{
    auto header = [](std::string key, const std::string& text)
    {
        return ya::ui::text(std::move(key)).setText(text).setFontSize(13).setColor(kHeaderColor);
    };
    auto body = [](std::string key, const std::string& text)
    {
        return ya::ui::text(std::move(key)).setText(text).setFontSize(13).setColor(kTextColor);
    };

    auto grid = ya::ui::row("RoundedGrid").setSpacing(12.0f);
    struct Card
    {
        const char* name;
        const char* label;
        glm::vec4   color;
        float       radius;
    };
    static constexpr Card kCards[] = {
        {"Round0", "0px", {0.37f, 0.18f, 0.18f, 1.0f}, 0.0f},
        {"Round8", "8px", {0.18f, 0.33f, 0.24f, 1.0f}, 8.0f},
        {"Round16", "16px", {0.18f, 0.25f, 0.38f, 1.0f}, 16.0f},
        {"Round32", "32px", {0.36f, 0.30f, 0.14f, 1.0f}, 32.0f},
    };
    for (const auto& c : kCards) {
        grid.child(
            ya::ui::panel(c.name)
                .setColor(c.color)
                .setCornerRadius(c.radius)
                .child(body(std::format("{}_Body", c.name), std::format("r={}", c.label))
                           .setHAlign(ya::EWidgetAlignH::Center)
                           .setVAlign(ya::EWidgetAlignV::Center),
                       ya::ui::canvasSlot().fill()),
            ya::ui::boxSlot().preferredSize({120.0f, 96.0f}));
    }

    auto missing = std::make_shared<ya::UIImage>("MissingImage");
    missing->setResourceMissing(true);

    auto diskImage = std::make_shared<ya::UIImage>("DiskImage");
    auto pathField = ya::ui::textField("DiskImagePath").setFontSize(13).setText("");
    auto pathRef   = pathField.share();
    auto loadBtn   = ya::ui::button("DiskImageLoad")
                       .child(ya::ui::text("DiskImageLoad_Label")
                                  .setText("Load")
                                  .setFontSize(13)
                                  .setHAlign(ya::EWidgetAlignH::Center)
                                  .setVAlign(ya::EWidgetAlignV::Center))
                       .setOnClick([diskImage, pathRef, log]
                                   {
                                       diskImage->setAssetPath(pathRef->getText());
                                       log(std::format("Image path -> '{}'", pathRef->getText()));
                                   });

    auto vectorCanvas = std::make_shared<FVectorDemoCanvas>("GalleryVectorCanvas");

    auto form = ya::ui::column("BrushForm")
                    .setPadding({16.0f, 12.0f})
                    .setSpacing(12.0f)
                    .child(header("BrushTitle", "Brush — solid / image / nine-patch / border, rounded rect, vectors"))
                    .child(header("RoundedTitle", "Rounded Rect — SDF corner radius"))
                    .child(std::move(grid), ya::ui::boxSlot().preferredSize({0.0f, 96.0f}))
                    .child(ya::ui::panel("RoundedNested")
                            .setColor({0.16f, 0.20f, 0.28f, 1.0f})
                            .setCornerRadius(20.0f)
                            .child(header("RoundedNestedCaption", "Rounded container with a sharp inner panel"),
                                   ya::ui::canvasSlot().anchor({0.10f, 0.20f}, {0.90f, 0.45f}))
                            .child(ya::ui::panel("RoundedNestedInner")
                                       .setColor({0.55f, 0.60f, 0.68f, 1.0f}),
                                   ya::ui::canvasSlot().anchor({0.10f, 0.55f}, {0.90f, 0.85f})),
                           ya::ui::boxSlot().preferredSize({280.0f, 110.0f}))
                    .child(header("BrushKindsTitle", "FBrush draw types"))
                    .child(ya::ui::row("BrushKindsRow")
                            .setSpacing(10.0f)
                            .child(ya::ui::panel("BrushSolid")
                                    .setStyleField("fillColor", ya::FBrush::solid({0.28f, 0.40f, 0.32f, 1.0f}))
                                    .child(body("BrushSolid_Body", "solid")
                                               .setHAlign(ya::EWidgetAlignH::Center)
                                               .setVAlign(ya::EWidgetAlignV::Center),
                                           ya::ui::canvasSlot().fill()),
                                   ya::ui::boxSlot().preferredSize({110.0f, 72.0f}))
                            .child(ya::ui::panel("BrushImage")
                                    .setStyleField("fillColor", ya::FBrush::image("builtin/checkerboard"))
                                    .child(body("BrushImage_Body", "image")
                                               .setHAlign(ya::EWidgetAlignH::Center)
                                               .setVAlign(ya::EWidgetAlignV::Center),
                                           ya::ui::canvasSlot().fill()),
                                   ya::ui::boxSlot().preferredSize({110.0f, 72.0f}))
                            .child(ya::ui::panel("BrushNinePatch")
                                    .setStyleField("fillColor",
                                                   ya::FBrush::ninePatch("builtin/checkerboard", {8.0f, 8.0f, 8.0f, 8.0f}))
                                    .child(body("BrushNinePatch_Body", "nine-patch")
                                               .setHAlign(ya::EWidgetAlignH::Center)
                                               .setVAlign(ya::EWidgetAlignV::Center),
                                           ya::ui::canvasSlot().fill()),
                                   ya::ui::boxSlot().preferredSize({110.0f, 72.0f}))
                            .child(ya::ui::panel("BrushBorder")
                                    .setStyleField("fillColor",
                                                   ya::FBrush::border("builtin/checkerboard", {8.0f, 8.0f, 8.0f, 8.0f}))
                                    .child(body("BrushBorder_Body", "border")
                                               .setHAlign(ya::EWidgetAlignH::Center)
                                               .setVAlign(ya::EWidgetAlignV::Center),
                                           ya::ui::canvasSlot().fill()),
                                   ya::ui::boxSlot().preferredSize({110.0f, 72.0f})))
                    .child(header("ImageStatesTitle", "UIImage placeholder vs missing"))
                    .child(ya::ui::row("ImageStatesRow")
                            .setSpacing(10.0f)
                            .child(ya::ui::image("PlaceholderImage"), ya::ui::boxSlot().preferredSize({96.0f, 64.0f}))
                            .child(missing, ya::ui::boxSlot().preferredSize({96.0f, 64.0f}))
                            .child(body("ImageStatesHint", "left = placeholderFill (empty path); right = errorFill (missing)")))
                    .child(header("DiskImageTitle", "Load an image file from disk"))
                    .child(body("DiskImageHint",
                                "Host IGuiTextureSource loads absolute paths or file: URIs (widgets never touch AssetManager)."))
                    .child(ya::ui::row("DiskImageRow")
                               .setSpacing(8.0f)
                               .child(std::move(pathField), ya::ui::boxSlot().preferredSize({360.0f, 26.0f}))
                               .child(std::move(loadBtn), ya::ui::boxSlot().preferredSize({80.0f, 26.0f})))
                    .child(diskImage, ya::ui::boxSlot().preferredSize({160.0f, 96.0f}))
                    .child(header("VectorTitle", "Vector primitives — lines, outline, bezier"))
                    .child(vectorCanvas,
                           ya::FBoxSlotArgs{.crossAlignment = ya::EUIBoxSlotCrossAlignment::Start,
                                            .preferredSize  = {430.0f, 110.0f}});
    auto page = ya::ui::panel("BrushDemo").setColor(kPanelColor).child(std::move(form), ya::ui::canvasSlot().fill());
    ya::ui::build(tree, parent, std::move(page), ya::ui::canvasSlot().fill());
    state.statusText = "Brush demo built";
    (void)log;
}

} // namespace guiworkbench
