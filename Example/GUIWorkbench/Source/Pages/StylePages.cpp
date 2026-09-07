#include "../WorkbenchDemoPages.h"
#include "DemoPageCommon.h"

#include "GUI/Declarative/Build.h"
#include "GUI/Widgets/Brush.h"

#include <format>
#include <memory>

namespace guiworkbench
{

void buildThemeDemo(ya::WidgetTree& tree, ya::UIElement& parent, FDemoState& state,
                    const std::function<void(const std::string&)>& log,
                    const std::function<void(bool bDark)>& /*onToggleTheme*/)
{
    auto header = [](std::string key, const std::string& text, uint32_t fontSize = 13)
    {
        return ya::ui::text(std::move(key)).setText(text).setFontSize(fontSize).setColor(kHeaderColor);
    };
    auto themedButton = [](std::string key, const std::string& label)
    {
        std::string labelKey = key + "_Label";
        return ya::ui::button(std::move(key))
            .child(ya::ui::text(std::move(labelKey))
                       .setText(label)
                       .setFontSize(13)
                       .setHAlign(ya::EWidgetAlignH::Center)
                       .setVAlign(ya::EWidgetAlignV::Center));
    };

    auto page = ya::ui::column("ThemeForm")
                    .setPadding({12.0f, 12.0f})
                    .setSpacing(8.0f)
                    .child(header("ThemeTitle", "Theme — View menu swaps the tree UITheme; samples overlay sub-styles"))
                    .child(header("ThemeHint", "Use View → Dark Theme / Light Theme. Controls below share keys or setStyleField overlays.", 12))
                    .child(themedButton("ThemeShowButton", "Themed button"), ya::ui::boxSlot().preferredSize({180.0f, 26.0f}))
                    .child(themedButton("ThemeAccentButton", "Accent overlay")
                               .setStyleField("normalFill", ya::FBrush::solid({0.20f, 0.38f, 0.72f, 1.0f}))
                               .setStyleField("hoveredFill", ya::FBrush::solid({0.28f, 0.50f, 0.86f, 1.0f}))
                               .setStyleField("pressedFill", ya::FBrush::solid({0.14f, 0.28f, 0.58f, 1.0f})),
                           ya::ui::boxSlot().preferredSize({180.0f, 26.0f}))
                    .child(header("ThemeButtonHint", "Default uses key \"button\"; Accent overlays fill colors on that key", 11))
                    .child(ya::ui::checkBox("ThemeCheck")
                               .setChecked(true)
                               .child(ya::ui::text("ThemeCheck_Body").setText("Checkbox (key checkbox)").setFontSize(13))
                               .setOnChanged([log](bool bChecked) { log(std::format("Theme checkbox -> {}", bChecked)); }))
                    .child(ya::ui::slider("ThemeSlider").setValue(0.45f).setOnValueChanged(
                               [log](float value) { log(std::format("Theme slider -> {:.2f}", value)); }),
                           ya::ui::boxSlot().preferredSize({220.0f, 22.0f}))
                    .child(ya::ui::row("ThemePanelRow")
                               .setSpacing(10.0f)
                               .child(ya::ui::panel("ThemeShowPanel")
                                          .child(header("ThemeShowCaption", "panel", 11)
                                                     .setHAlign(ya::EWidgetAlignH::Center)
                                                     .setVAlign(ya::EWidgetAlignV::Center),
                                                 ya::ui::canvasSlot().fill()),
                                      ya::ui::boxSlot().preferredSize({140.0f, 48.0f}))
                               .child(ya::ui::panel("ThemeCanvasPanel")
                                          .setStyleKey("panel.canvas")
                                          .child(header("ThemeCanvasCaption", "panel.canvas", 11)
                                                     .setHAlign(ya::EWidgetAlignH::Center)
                                                     .setVAlign(ya::EWidgetAlignV::Center),
                                                 ya::ui::canvasSlot().fill()),
                                      ya::ui::boxSlot().preferredSize({140.0f, 48.0f}))
                               .child(ya::ui::panel("ThemeOutlinedPanel")
                                          .setStyleField("fillColor", ya::FBrush::solid({0.16f, 0.18f, 0.22f, 1.0f}))
                                          .setStyleField("outlineColor", glm::vec4{0.50f, 0.56f, 0.70f, 1.0f})
                                          .setStyleField("outlineThickness", 1.0f)
                                          .child(header("ThemeOutlinedCaption", "outlined", 11)
                                                     .setHAlign(ya::EWidgetAlignH::Center)
                                                     .setVAlign(ya::EWidgetAlignV::Center),
                                                 ya::ui::canvasSlot().fill()),
                                      ya::ui::boxSlot().preferredSize({140.0f, 48.0f})));
    ya::ui::build(tree, parent, std::move(page), ya::ui::canvasSlot().fill());
    (void)state;
}

} // namespace guiworkbench
