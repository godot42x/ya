#include "../WorkbenchDemoPages.h"
#include "DemoPageCommon.h"

#include "GUI/Declarative/Build.h"
#include "GUI/Layout/UILayout.h"
#include "GUI/Widgets/Controls/Image.h"
#include "GUI/Widgets/Controls/Slider.h"

#include <format>

namespace guiworkbench
{

void buildLayoutDemo(ya::WidgetTree& tree, ya::UICanvasPanel& parent, FDemoState& state,
                     const std::function<void(const std::string&)>& log)
{
    auto header = [](std::string key, const std::string& text)
    {
        return ya::ui::text(std::move(key)).setText(text).setFontSize(13).setStyleKey(std::string(ya::StyleKey::TextMuted));
    };
    auto body = [](std::string key, const std::string& text)
    {
        return ya::ui::text(std::move(key)).setText(text).setFontSize(13).setStyleKey(std::string(ya::StyleKey::Text));
    };
    auto cellLabel = [&](std::string key, const std::string& text)
    {
        return body(std::move(key), text)
            .setHAlign(ya::EWidgetAlignH::Center)
            .setVAlign(ya::EWidgetAlignV::Center);
    };

    auto hbox = ya::ui::row("DemoHBox")
                    .setSpacing(state.layoutSpacing)
                    .setPadding({4.0f, 4.0f})
                    .setClipChildren(true);
    for (int i = 0; i < 3; ++i) {
        hbox.child(
            ya::ui::border(std::format("HCell{}", i))
                .setColor({0.22f + i * 0.06f, 0.30f + i * 0.04f, 0.38f, 1.0f})
                .child(cellLabel(std::format("HCell{}_Body", i), std::format("Cell {}", i + 1)),
                       ya::ui::contentSlot().fill()),
            ya::ui::boxSlot().fill(1.0f).preferredSize({0.0f, 50.0f}));
    }

    auto vbox = ya::ui::column("DemoVBox")
                    .setSpacing(state.layoutSpacing)
                    .setPadding({6.0f, 6.0f})
                    .setMainAxisAlignment(ya::EWidgetMainAxisAlignment::End)
                    .setClipChildren(true);
    for (int i = 0; i < 4; ++i) {
        vbox.child(
            ya::ui::border(std::format("VCell{}", i))
                .setColor({0.30f + i * 0.05f, 0.22f, 0.42f, 1.0f})
                .child(cellLabel(std::format("VCell{}_Body", i), std::format("Row {}", i + 1)),
                       ya::ui::contentSlot().fill()),
            ya::ui::boxSlot().preferredSize({100.0f, 50.0f}));
    }

    auto hboxRef = hbox.share();
    auto vboxRef = vbox.share();
    auto form    = ya::ui::column("LayoutForm")
                    .setPadding({16.0f, 12.0f})
                    .setSpacing(10.0f)
                    .child(header("LayoutTitle",
                               "Box — VBox / HBox, spacing, padding, Auto / Fill / weight, Hidden"))
                    .child(body("LayoutHint", "Resize the window: containers stretch via the parent-owned slot."))
                    .child(header("HBoxTitle", "HBox (horizontal container)"))
                    .child(std::move(hbox), ya::ui::boxSlot().preferredSize({0.0f, 64.0f}))
                    .child(header("VBoxTitle", "VBox with End alignment"))
                    .child(std::move(vbox), ya::ui::boxSlot().preferredSize({0.0f, 140.0f}))
                    .child(ya::ui::row("SpacingRow")
                            .setSpacing(8.0f)
                            .child(body("SpacingLabel", "Spacing"))
                            .child(ya::ui::slider("SpacingSlider")
                                    .setValue(state.layoutSpacing / 24.0f)
                                    .setOnValueChanged([&state, log, hboxRef, vboxRef](float value)
                                                       {
                                                state.layoutSpacing = value * 24.0f;
                                                if (hboxRef) {
                                                    hboxRef->setSpacing(state.layoutSpacing);
                                                }
                                                if (vboxRef) {
                                                    vboxRef->setSpacing(state.layoutSpacing);
                                                }
                                                log(std::format("Spacing -> {:.1f}px", state.layoutSpacing)); }),
                                ya::ui::boxSlot().preferredSize({220.0f, 22.0f})))
                    .child(header("FillTitle", "Auto vs Fill vs weight"))
                    .child(ya::ui::row("FillWeightRow")
                            .setSpacing(6.0f)
                            .child(ya::ui::border("AutoCell")
                                    .setColor({0.28f, 0.22f, 0.20f, 1.0f})
                                    .child(cellLabel("AutoCell_Body", "Auto"), ya::ui::contentSlot().fill()),
                                   ya::ui::boxSlot().autoSize().preferredSize({80.0f, 36.0f}))
                            .child(ya::ui::border("FillCellA")
                                    .setColor({0.20f, 0.28f, 0.22f, 1.0f})
                                    .child(cellLabel("FillCellA_Body", "Fill 1"), ya::ui::contentSlot().fill()),
                                   ya::ui::boxSlot().fill(1.0f).preferredSize({0.0f, 36.0f}))
                            .child(ya::ui::border("FillCellB")
                                    .setColor({0.20f, 0.22f, 0.30f, 1.0f})
                                    .child(cellLabel("FillCellB_Body", "Fill 2"), ya::ui::contentSlot().fill()),
                                   ya::ui::boxSlot().fill(2.0f).preferredSize({0.0f, 36.0f})),
                           ya::ui::boxSlot().preferredSize({0.0f, 40.0f}))
                    .child(header("HiddenTitle", "Visibility vs layout space"))
                    .child(ya::ui::row("VisibilityRow")
                            .setSpacing(6.0f)
                            .child(ya::ui::border("VisVisible")
                                    .setColor({0.22f, 0.32f, 0.24f, 1.0f})
                                    .child(cellLabel("VisVisible_Body", "Visible"), ya::ui::contentSlot().fill()),
                                   ya::ui::boxSlot().preferredSize({90.0f, 36.0f}))
                            .child(ya::ui::border("VisHidden")
                                    .setColor({0.32f, 0.24f, 0.20f, 1.0f})
                                    .setVisibility(ya::EWidgetVisibility::Hidden)
                                    .child(cellLabel("VisHidden_Body", "Hidden"), ya::ui::contentSlot().fill()),
                                   ya::ui::boxSlot().preferredSize({90.0f, 36.0f}))
                            .child(ya::ui::border("VisCollapsed")
                                    .setColor({0.24f, 0.20f, 0.32f, 1.0f})
                                    .setVisibility(ya::EWidgetVisibility::Collapsed)
                                    .child(cellLabel("VisCollapsed_Body", "Collapsed"), ya::ui::contentSlot().fill()),
                                   ya::ui::boxSlot().preferredSize({90.0f, 36.0f}))
                            .child(body("VisibilityHint", "Hidden keeps a gap; Collapsed does not.")),
                           ya::ui::boxSlot().preferredSize({0.0f, 40.0f}));
    auto page = ya::ui::border("LayoutDemo").setStyleKey(std::string(ya::StyleKey::Panel)).child(std::move(form), ya::ui::contentSlot().fill());
    (void)ya::ui::attach(tree, parent, std::move(page).release(), ya::ui::canvasSlot().fill());
}

void buildHostsDemo(ya::WidgetTree& tree, ya::UICanvasPanel& parent, FDemoState& state,
                    const std::function<void(const std::string&)>& log)
{
    auto header = [](std::string key, const std::string& text)
    {
        return ya::ui::text(std::move(key)).setText(text).setFontSize(13).setStyleKey(std::string(ya::StyleKey::TextMuted));
    };
    auto body = [](std::string key, const std::string& text)
    {
        return ya::ui::text(std::move(key)).setText(text).setFontSize(13).setStyleKey(std::string(ya::StyleKey::Text));
    };
    auto cell = [&](std::string key, const std::string& text, const glm::vec4& color)
    {
        const std::string bodyKey = key + "_Body";
        return ya::ui::border(std::move(key))
            .setColor(color)
            .child(body(bodyKey, text)
                       .setHAlign(ya::EWidgetAlignH::Center)
                       .setVAlign(ya::EWidgetAlignV::Center),
                   ya::ui::contentSlot().fill());
    };

    auto overlay = ya::ui::overlay("OverlayDemo")
                       .child(cell("OverlayFill", "Fill", {0.18f, 0.22f, 0.30f, 1.0f}),
                              ya::ui::overlaySlot().fill())
                       .child(cell("OverlayStart", "Start", {0.36f, 0.22f, 0.20f, 1.0f}),
                              ya::ui::overlaySlot()
                                  .align(ya::EUIOverlayAlignment::Start, ya::EUIOverlayAlignment::Start)
                                  .preferredSize({110.0f, 28.0f})
                                  .inset(ya::FMargin::all(8.0f)))
                       .child(cell("OverlayCenter", "Center", {0.20f, 0.32f, 0.24f, 1.0f}),
                              ya::ui::overlaySlot()
                                  .align(ya::EUIOverlayAlignment::Center, ya::EUIOverlayAlignment::Center)
                                  .preferredSize({110.0f, 28.0f}))
                       .child(cell("OverlayEnd", "End", {0.22f, 0.24f, 0.38f, 1.0f}),
                              ya::ui::overlaySlot()
                                  .align(ya::EUIOverlayAlignment::End, ya::EUIOverlayAlignment::End)
                                  .preferredSize({110.0f, 28.0f})
                                  .inset(ya::FMargin::all(8.0f)));

    auto form = ya::ui::column("HostsForm")
                    .setPadding({16.0f, 12.0f})
                    .setSpacing(10.0f)
                    .child(header("HostsTitle", "Layout hosts — Overlay, SizeBox, Canvas anchors"))
                    .child(header("OverlayTitle", "UIOverlay — independent alignment in one rect"))
                    .child(std::move(overlay), ya::ui::boxSlot().preferredSize({0.0f, 140.0f}))
                    .child(header("SizeBoxTitle", "UISizeBox — width/height override + min/max"))
                    .child(ya::ui::row("SizeBoxRow")
                            .setSpacing(12.0f)
                            .child(ya::ui::sizeBox("SizeBoxWide")
                                    .setWidth(160.0f)
                                    .setHeight(48.0f)
                                    .child(cell("SizeBoxWideInner", "160 x 48", {0.24f, 0.30f, 0.22f, 1.0f}),
                                           ya::ui::contentSlot().fill()))
                            .child(ya::ui::sizeBox("SizeBoxClamped")
                                    .setMinSize({80.0f, 32.0f})
                                    .setMaxSize({120.0f, 48.0f})
                                    .child(cell("SizeBoxClampedInner", "min/max", {0.30f, 0.22f, 0.28f, 1.0f}),
                                           ya::ui::contentSlot().fill())))
                    .child(header("CanvasTitle", "Canvas — stretch anchors vs SizeToContent"))
                    .child(ya::ui::canvasPanel("CanvasAnchorDemo")
                            .child(ya::ui::border("CanvasAnchorDemoFill")
                                       .setColor({0.14f, 0.16f, 0.20f, 1.0f})
                                       .setVisibility(ya::EWidgetVisibility::HitTestInvisible),
                                   ya::ui::canvasSlot().fill())
                            .child(ya::ui::border("CanvasStretch")
                                    .setColor({0.22f, 0.28f, 0.38f, 1.0f})
                                    .child(body("CanvasStretch_Body", "stretch 10%..90%")
                                               .setHAlign(ya::EWidgetAlignH::Center)
                                               .setVAlign(ya::EWidgetAlignV::Center),
                                           ya::ui::contentSlot().fill()),
                                   ya::ui::canvasSlot().anchor({0.10f, 0.15f}, {0.90f, 0.55f}))
                            .child(ya::ui::text("CanvasAuto")
                                    .setText("Auto size, top-left")
                                    .setFontSize(13)
                                    .setStyleKey(std::string(ya::StyleKey::Text)),
                                   ya::ui::canvasSlot().offset({12.0f, 88.0f})),
                           ya::ui::boxSlot().preferredSize({0.0f, 140.0f}));
    auto page = ya::ui::border("HostsDemo").setStyleKey(std::string(ya::StyleKey::Panel)).child(std::move(form), ya::ui::contentSlot().fill());
    (void)ya::ui::attach(tree, parent, std::move(page).release(), ya::ui::canvasSlot().fill());
    (void)state;
    (void)log;
}

void buildScrollSplitDemo(ya::WidgetTree& tree, ya::UICanvasPanel& parent, FDemoState& state,
                          const std::function<void(const std::string&)>& log)
{
    auto header = [](std::string key, const std::string& text)
    {
        return ya::ui::text(std::move(key)).setText(text).setFontSize(13).setStyleKey(std::string(ya::StyleKey::TextMuted));
    };
    auto body = [](std::string key, const std::string& text)
    {
        return ya::ui::text(std::move(key)).setText(text).setFontSize(13).setStyleKey(std::string(ya::StyleKey::Text));
    };

    auto list = ya::ui::column("DemoScrollList")
                    .setSpacing(2.0f)
                    .setPadding({6.0f, 6.0f});
    for (int i = 0; i < 40; ++i) {
        list.child(
            ya::ui::border(std::format("ScrollRow{}", i))
                .setColor({0.18f + (i % 3) * 0.04f, 0.20f, 0.24f, 1.0f})
                .setPadding(ya::FMargin{8.0f, 0.0f, 0.0f, 0.0f})
                .child(body(std::format("ScrollRow{}_Body", i), std::format("Scrollable entry {}", i + 1))
                           .setVAlign(ya::EWidgetAlignV::Center),
                       ya::ui::contentSlot().fill()),
            ya::ui::boxSlot().preferredSize({0.0f, 24.0f}));
    }

    auto innerList = ya::ui::column("InnerScrollList").setSpacing(2.0f).setPadding({6.0f, 6.0f});
    for (int i = 0; i < 16; ++i) {
        innerList.child(body(std::format("InnerRow{}_Body", i), std::format("Nested row {}", i + 1)),
                        ya::ui::boxSlot().preferredSize({0.0f, 20.0f}));
    }

    auto split = ya::ui::splitPane("DemoSplit")
                     .setSplitRatio(0.38f)
                     .setMinFirstExtent(120.0f)
                     .setMinSecondExtent(160.0f)
                     .children(
                         ya::ui::scroll("DemoScroll").child(std::move(list)),
                         ya::ui::column("DemoSplitRight")
                             .setSpacing(8.0f)
                             .child(ya::ui::border("DemoSplitRightHeader")
                                     .setColor({0.24f, 0.30f, 0.40f, 1.0f})
                                     .child(body("DemoSplitRight_Body",
                                                 "Drag the divider. Inner scroll bubbles unhandled wheel to the outer pane.")
                                                .setHAlign(ya::EWidgetAlignH::Center)
                                                .setVAlign(ya::EWidgetAlignV::Center),
                                            ya::ui::contentSlot().fill()),
                                    ya::ui::boxSlot().preferredSize({0.0f, 64.0f}))
                             .child(ya::ui::scroll("InnerScroll").child(std::move(innerList)),
                                    ya::ui::boxSlot().fill()));

    auto layout = ya::ui::column("ScrollSplitLayout")
                      .setPadding({16.0f, 12.0f})
                      .setSpacing(10.0f)
                      .children(
                          header("ScrollSplitTitle", "Scroll viewport + split pane — drag the divider"),
                          body("ScrollSplitHint",
                               "The split stretches with the window; hover the divider to grab it. Nested scroll returns unhandled wheel at its bounds."));
    layout.child(std::move(split), ya::ui::boxSlot().fill());

    auto page = ya::ui::border("ScrollSplitDemo")
                    .setStyleKey(std::string(ya::StyleKey::Panel))
                    .child(std::move(layout), ya::ui::contentSlot().fill());
    (void)ya::ui::attach(tree, parent, std::move(page).release(), ya::ui::canvasSlot().fill());
    (void)state;
    (void)log;
}

} // namespace guiworkbench
