#pragma once

#include "GUI/Widgets/Controls/DockSpace/DockNode.h"

#include <cstddef>

namespace ya
{

/// Where a dock-panel drag is hovering. `UIDockTabWell` and `UIDockTabStack`
/// are the hit targets; DockArea (`UIDockSpace`) materializes stacks, paints
/// the chooser overlay, and applies `commitDrop`. Split gutters fall through
/// to the Area. Floating windows reuse the same kinds with `floatingWindowId`
/// set.
enum class EDockDropTargetKind : uint8_t
{
    /// Pointer is on a tab strip: insert / reorder in that stack.
    TabWell,
    /// Pointer is on a stack's center merge chooser block.
    TabStackCenter,
    /// Pointer is on a stack's north/south/east/west chooser block.
    TabStackSplit,
    /// Pointer is over a foreign stack but not on a block: show chooser,
    /// do not commit.
    TabStackChooser,
    /// Pointer is over another in-process floating window: merge as a tab.
    FloatingTabWell,
    /// Pointer is outside every stack/well: show source chooser dimmed;
    /// drop becomes tear-off / NoTarget.
    NoTarget,
};

struct FDockDropTarget
{
    EDockDropTargetKind   kind             = EDockDropTargetKind::NoTarget;
    DockNodeId            stackId          = kInvalidDockNodeId;
    FDockFloatingWindowId floatingWindowId = kInvalidFloatingWindowId;
    size_t                insertIndex      = SIZE_MAX;
    EDockCardinalSide     splitSide        = EDockCardinalSide::West;

    [[nodiscard]] static FDockDropTarget well(DockNodeId stackId, size_t insertIndex)
    {
        FDockDropTarget target;
        target.kind        = EDockDropTargetKind::TabWell;
        target.stackId     = stackId;
        target.insertIndex = insertIndex;
        return target;
    }

    [[nodiscard]] static FDockDropTarget stackCenter(DockNodeId stackId)
    {
        FDockDropTarget target;
        target.kind    = EDockDropTargetKind::TabStackCenter;
        target.stackId = stackId;
        return target;
    }

    [[nodiscard]] static FDockDropTarget stackSplit(DockNodeId stackId, EDockCardinalSide side)
    {
        FDockDropTarget target;
        target.kind      = EDockDropTargetKind::TabStackSplit;
        target.stackId   = stackId;
        target.splitSide = side;
        return target;
    }

    [[nodiscard]] static FDockDropTarget stackChooser(DockNodeId stackId)
    {
        FDockDropTarget target;
        target.kind    = EDockDropTargetKind::TabStackChooser;
        target.stackId = stackId;
        return target;
    }

    [[nodiscard]] static FDockDropTarget floatingWell(FDockFloatingWindowId floatingWindowId)
    {
        FDockDropTarget target;
        target.kind             = EDockDropTargetKind::FloatingTabWell;
        target.floatingWindowId = floatingWindowId;
        return target;
    }

    [[nodiscard]] static FDockDropTarget noTarget(DockNodeId stackId)
    {
        FDockDropTarget target;
        target.kind    = EDockDropTargetKind::NoTarget;
        target.stackId = stackId;
        return target;
    }

    [[nodiscard]] bool commitsDrop() const
    {
        switch (kind) {
        case EDockDropTargetKind::TabWell:
        case EDockDropTargetKind::TabStackCenter:
        case EDockDropTargetKind::TabStackSplit:
        case EDockDropTargetKind::FloatingTabWell: {
            return true;
        }
        case EDockDropTargetKind::TabStackChooser:
        case EDockDropTargetKind::NoTarget: {
            return false;
        }
        }
        return false;
    }

    [[nodiscard]] bool isPreviewOnly() const { return !commitsDrop(); }

    [[nodiscard]] bool isMerge() const
    {
        switch (kind) {
        case EDockDropTargetKind::TabWell:
        case EDockDropTargetKind::TabStackCenter:
        case EDockDropTargetKind::FloatingTabWell: {
            return true;
        }
        case EDockDropTargetKind::TabStackSplit:
        case EDockDropTargetKind::TabStackChooser:
        case EDockDropTargetKind::NoTarget: {
            return false;
        }
        }
        return false;
    }
};

[[nodiscard]] inline const char* dockDropTargetKindName(EDockDropTargetKind kind)
{
    switch (kind) {
    case EDockDropTargetKind::TabWell: {
        return "tabWell";
    }
    case EDockDropTargetKind::TabStackCenter: {
        return "tabStackCenter";
    }
    case EDockDropTargetKind::TabStackSplit: {
        return "tabStackSplit";
    }
    case EDockDropTargetKind::TabStackChooser: {
        return "tabStackChooser";
    }
    case EDockDropTargetKind::FloatingTabWell: {
        return "floatingTabWell";
    }
    case EDockDropTargetKind::NoTarget: {
        return "noTarget";
    }
    }
    return "unknown";
}

enum class EDockDropCommit : uint8_t
{
    Rejected,
    /// Same-stack content drop: select the tab, do not rebuild layout.
    Selected,
    Applied,
};

} // namespace ya
