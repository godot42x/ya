#pragma once

// Shared readout building blocks for the editor's live-stat panels: a "stat
// cell" (muted label over a bright, larger value) for the pinned KPI strip, a
// "stat row" (fixed label column + value) for a section body, and the number
// formatters both use.
//
// Before this, every panel formatted its own sentence ("GPU memory: 290 MiB in
// 144 allocations") in the same muted style as its labels, so the numbers a
// reader came for had no more weight than the prose around them and two panels
// could not be scanned down a common column. One geometry + one unit policy is
// what lets the reader's eye land on the value instead of hunting for it.

#include "GUI/Declarative/Build.h"
#include "GUI/Widgets/Controls/Text.h"
#include "GameEditor/UI/Shell/EditorTheme.h"

#include <cstdint>
#include <format>
#include <memory>
#include <string>
#include <string_view>

namespace ya
{

/// One live number plus the widget that paints it. The panel mounts `root`
/// once and writes `value` from its per-frame sync.
struct FStatReadout
{
    UIElementRef            root;
    std::shared_ptr<UIText> value;
};

/// A size a reader compares against another size keeps one decimal below
/// 100 MiB and none above: "12.3 / 45.6 / 789 MiB" scans as one ladder, where
/// "12.34 / 45.67 / 789.01" makes each row a separate read.
[[nodiscard]] inline std::string formatMiB(uint64_t bytes)
{
    const double mib = static_cast<double>(bytes) / (1024.0 * 1024.0);
    return mib < 100.0 ? std::format("{:.1f} MiB", mib) : std::format("{:.0f} MiB", mib);
}

/// Frame and pass times are read to the hundredth of a millisecond -- the
/// resolution the GPU timestamps and the CPU stage timers actually carry.
[[nodiscard]] inline std::string formatMs(float milliseconds)
{
    return std::format("{:.2f} ms", milliseconds);
}

[[nodiscard]] inline std::string formatFps(float fps)
{
    return std::format("{:.1f}", fps);
}

/// A KPI cell: a muted label over a bright, larger value. Used across a row in
/// the pinned strip, so the value's size -- not a colored box -- is what makes
/// it the thing the eye lands on first.
[[nodiscard]] inline FStatReadout makeStatCell(const std::string& key, std::string_view label)
{
    auto value = ui::text(key + "Value")
                     .setText("-")
                     .setStyleKey(editorStyle(StyleKey::TextHeader))
                     .setHAlign(EWidgetAlignH::Left)
                     .setVAlign(EWidgetAlignV::Center);
    auto cell = ui::column(key)
                    .setSpacing(2.0f)
                    .child(ui::text(key + "Label")
                               .setText(std::string(label))
                               .setStyleKey("text.muted")
                               .setVAlign(EWidgetAlignV::Center))
                    .child(value);
    return FStatReadout{.root = cell.release(), .value = value.share()};
}

/// A stat row: muted label in a fixed column, value to its right. The fixed
/// column is what aligns every value in a section down one edge, so two rows
/// can be compared without reading either label.
[[nodiscard]] inline FStatReadout makeStatRow(const std::string& key, std::string_view label)
{
    auto value = ui::text(key + "Value")
                     .setText("-")
                     .setStyleKey(editorStyle(StyleKey::Text))
                     .setVAlign(EWidgetAlignV::Center);
    auto row = ui::row(key)
                   .setSpacing(editor_density::kControlSpacing)
                   .child(ui::text(key + "Label")
                              .setText(std::string(label))
                              .setStyleKey("text.muted")
                              .setVAlign(EWidgetAlignV::Center),
                          ui::boxSlot().preferredSize({editor_density::kLabelColumn,
                                                       editor_density::kRowHeight}))
                   .child(value, ui::boxSlot().fillWidth().preferredSize({0.0f, editor_density::kRowHeight}));
    return FStatReadout{.root = row.release(), .value = value.share()};
}

} // namespace ya
