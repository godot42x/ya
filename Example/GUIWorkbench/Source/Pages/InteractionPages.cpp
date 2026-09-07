#include "../WorkbenchDemoPages.h"
#include "DemoPageCommon.h"

#include "GUI/Declarative/Build.h"
#include "GUI/Binding/Reactive.h"

#include <format>
#include <memory>
#include <vector>

namespace guiworkbench
{

void buildDragDropDemo(ya::WidgetTree& tree, ya::UIElement& parent, FDemoState& state,
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

    auto sourceRow = ya::ui::row("DragSourceRow").setSpacing(8.0f);
    const std::vector<std::string> payloads = {"asset.texture.diffuse", "asset.mesh.cube", "asset.material.pbr"};
    for (const std::string& payload : payloads) {
        auto item = makeDemoDragSource("Drag_" + payload, payload, payload);
        if (payload == payloads[0]) {
            state.dragItem = item;
        }
        sourceRow.child(std::move(item), ya::ui::boxSlot().autoSize().preferredSize({0.0f, 30.0f}));
    }

    auto zone        = makeDemoDropTarget("DropZone", "Drop zone", {}, [&state, log](const std::string& payload)
    {
        state.dropLog = std::format("Dropped '{}'", payload);
        log(state.dropLog);
    });
    state.dropZone = zone;

    auto galleryRow = ya::ui::row("GalleryDragSrcRow").setSpacing(8.0f);
    for (int i = 0; i < 3; ++i) {
        auto source = makeDemoDragSource(std::format("GalleryDragSrc{}", i),
                                         std::format("Item {}", i + 1),
                                         std::format("payload.{}", i + 1));
        galleryRow.child(source, ya::ui::boxSlot().preferredSize({110.0f, 26.0f}));
    }

    auto dropResult = std::make_shared<ya::Reactive<std::string>>("(drop something here)");
    auto dropZoneA  = makeDemoDropTarget("GalleryDropA",
                                         "Zone A: accepts any",
                                         {},
                                         [dropResult, log](const std::string& payload)
                                         {
                                             dropResult->set(std::format("Zone A <- {}", payload));
                                             log(std::format("Dropped '{}' on Zone A", payload));
                                         });
    auto dropZoneB = makeDemoDropTarget("GalleryDropB",
                                        "Zone B: only payload.2",
                                        [](const std::string& payload) { return payload == "payload.2"; },
                                        [dropResult, log](const std::string& payload)
                                        {
                                            dropResult->set(std::format("Zone B <- {}", payload));
                                            log(std::format("Dropped '{}' on Zone B", payload));
                                        });

    auto form = ya::ui::column("DragDropForm")
                    .setPadding({16.0f, 12.0f})
                    .setSpacing(10.0f)
                    .child(std::move(sourceRow))
                    .child(std::move(zone), ya::ui::boxSlot().preferredSize({0.0f, 120.0f}))
                    .child(header("GallerySection7", "Predicate targets (Zone B: only payload.2)"))
                    .child(std::move(galleryRow))
                    .child(ya::ui::row("GalleryDropRow")
                               .setSpacing(8.0f)
                               .child(ya::ui::text("GalleryDropResult").bindText(dropResult).setFontSize(13))
                               .child(dropZoneA, ya::ui::boxSlot().preferredSize({180.0f, 60.0f}))
                               .child(dropZoneB, ya::ui::boxSlot().preferredSize({180.0f, 60.0f})))
                    .child(body("GalleryDropHint", "Zone B: only payload.2"));
    auto page = ya::ui::panel("DragDropDemo")
                    .setColor(kPanelColor)
                    .child(std::move(form), ya::ui::canvasSlot().fill());
    ya::ui::build(tree, parent, std::move(page), ya::ui::canvasSlot().fill());
}

void buildEnableDemo(ya::WidgetTree& tree, ya::UIElement& parent, FDemoState& state,
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
    auto demoButton = [](std::string name, const std::string& label)
    {
        auto button = ya::ui::button(name).child(
            ya::ui::text(name + "_Label")
                .setText(label)
                .setFontSize(13)
                .setHAlign(ya::EWidgetAlignH::Center)
                .setVAlign(ya::EWidgetAlignV::Center));
        return std::move(button).setContentPadding({12.0f, 4.0f});
    };

    auto form = ya::ui::column("EnableForm").setPadding({16.0f, 12.0f}).setSpacing(12.0f);
    form.child(header("EnableTitle", "Enable & visibility — subtree disable plus five visibility modes"));
    form.child(header("InteractionsDisableHeader", "Subtree disable"));
    auto group = ya::ui::row("DisableGroup")
                     .setSpacing(8.0f)
                     .child(demoButton("GroupBtnA", "Group button A")
                               .setOnClick([log] { log("Group button A clicked"); }),
                           ya::ui::boxSlot().preferredSize({140.0f, 26.0f}))
                     .child(demoButton("GroupBtnB", "Group button B")
                               .setOnClick([log] { log("Group button B clicked"); }),
                           ya::ui::boxSlot().preferredSize({140.0f, 26.0f}));
    auto groupHandle   = group.share();
    auto bGroupEnabled = std::make_shared<bool>(true);
    form.child(std::move(group));
    form.child(demoButton("ToggleGroupBtn", "Toggle group enabled")
                   .setOnClick(
                       [groupHandle, bGroupEnabled, log]
                       {
                           *bGroupEnabled = !*bGroupEnabled;
                           groupHandle->setEnabled(*bGroupEnabled);
                           const std::string groupState = *bGroupEnabled ? "enabled" : "disabled";
                           log(std::format("Group {}", groupState));
                       }),
               ya::ui::boxSlot().preferredSize({200.0f, 26.0f}));

    form.child(header("VisibilityModesTitle", "EWidgetVisibility"));
    const struct Mode
    {
        const char*           name;
        const char*           label;
        ya::EWidgetVisibility value;
    } modes[] = {
        {"VisModeVisible", "Visible", ya::EWidgetVisibility::Visible},
        {"VisModeHidden", "Hidden", ya::EWidgetVisibility::Hidden},
        {"VisModeCollapsed", "Collapsed", ya::EWidgetVisibility::Collapsed},
        {"VisModeHitTestInvisible", "HitTestInvisible", ya::EWidgetVisibility::HitTestInvisible},
        {"VisModeSelfHitTestInvisible", "SelfHitTestInvisible", ya::EWidgetVisibility::SelfHitTestInvisible},
    };
    auto modeRow = ya::ui::row("VisibilityModesRow").setSpacing(8.0f);
    for (const auto& mode : modes) {
        modeRow.child(ya::ui::panel(mode.name)
                          .setColor({0.22f, 0.28f, 0.34f, 1.0f})
                          .setVisibility(mode.value)
                          .child(body(std::string(mode.name) + "_Body", mode.label)
                                     .setHAlign(ya::EWidgetAlignH::Center)
                                     .setVAlign(ya::EWidgetAlignV::Center),
                                 ya::ui::canvasSlot().fill()),
                      ya::ui::boxSlot().preferredSize({120.0f, 40.0f}));
    }
    form.child(std::move(modeRow), ya::ui::boxSlot().preferredSize({0.0f, 48.0f}));
    form.child(body("VisibilityModesHint",
                    "Hidden keeps layout space; Collapsed does not. HitTestInvisible still paints; children remain hittable."));

    auto page = ya::ui::panel("EnableDemo").setColor(kPanelColor).child(std::move(form), ya::ui::canvasSlot().fill());
    ya::ui::build(tree, parent, std::move(page), ya::ui::canvasSlot().fill());
    (void)state;
}

} // namespace guiworkbench
