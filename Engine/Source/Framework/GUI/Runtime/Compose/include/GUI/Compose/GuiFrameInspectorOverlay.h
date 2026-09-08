#pragma once

#include "Core/Api.h"
#include "GUI/Widgets/GuiFrameInspector.h"
#include "GUI/Widgets/UIFrameSnapshot.h"
#include "Render2D/Render2D.h"

namespace ya
{

struct GuiPerfStats;
struct WidgetTree;

/// Fill CPU model flush and optional GPU session stats into an inspector
/// record. Does not mutate the snapshot. Model flush is always written;
/// GPU fields are compiled out of `YA_PROFILING_DISABLED` builds.
YA_GUI_API void captureGuiComposeInspector(FGuiFrameInspectorRecord& record,
                                           const UIFrameSnapshot&    snapshot,
                                           const FRender2dFrameStats& gpuStats);

/// Occupancy-grid overdraw of snapshot items (target pixels). Empty snapshot
/// or empty extent yields zeros.
YA_GUI_API void captureGuiOverdrawInspector(FGuiFrameInspectorRecord& record,
                                            const UIFrameSnapshot&    snapshot,
                                            Extent2D                  framebuffer = {});

/// Draw HUD / rebuild flash / overdraw heatmap into the current Render2D
/// session. Must run inside begin()/end() after product snapshot replay.
/// Never writes into `snapshot.items`.
YA_GUI_API void emitGuiFrameInspectorOverlay(const FGuiFrameInspectorRecord& record,
                                             const UIFrameSnapshot&          snapshot,
                                             const GuiPerfStats&             perf);

/// Capture compose/overdraw stats from the live session (product replay already
/// flushed) and emit the overlay. No-op when the inspector is off or compiled
/// out. Call from `extraContent` so HUD GPU counts exclude overlay draws.
YA_GUI_API void runGuiFrameInspectorOverlay(WidgetTree&            tree,
                                            const UIFrameSnapshot& snapshot,
                                            Extent2D               framebuffer);

} // namespace ya
