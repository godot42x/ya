#pragma once

// ============================================================================
// FWorkbenchWorkspace - pure app-state workspace (gui-app-bootstrap Phase 3).
//
// Layering rule (plan 4.2): the workspace holds stable item ids, selection,
// field values and command results; it must NOT include GUI headers. The
// presenter (FWorkbenchApp) is the only layer allowed to know both the
// workspace and the widget ids/handles.
// ============================================================================

#include <glm/glm.hpp>

#include <string>
#include <vector>

namespace guiworkbench
{

/// One mock object in the workspace graph.
struct FWorkbenchItem
{
    std::string id;
    std::string name;
    bool        bVisible = true;
    glm::vec4   color    = {1.0f, 1.0f, 1.0f, 1.0f};
    glm::vec2   size     = {140.0f, 90.0f};
};

/// ToolWorkspace: app state / selection / commands (no UIElement include).
struct FWorkbenchWorkspace
{
    std::vector<FWorkbenchItem> items;
    std::string                 selectedId;
    bool                        bDirty        = false;
    std::string                 commandResult = "Ready";

    // === App commands (visible feedback through commandResult) ===
    void addItem(const std::string& name = "Item");
    void removeSelected();
    void renameSelected(const std::string& name);
    /// Restore the default mock object graph and select the first item.
    void resetLayout();

    // === Selection ===
    void select(const std::string& id);
    /// Move selection by `delta` rows (clamped; no-op without items).
    void selectRelative(int delta);
    [[nodiscard]] const FWorkbenchItem* getSelected() const;
    [[nodiscard]] FWorkbenchItem*       getSelectedMutable();
    [[nodiscard]] int                   getSelectedIndex() const;

    // === Inspector mutations (mark dirty) ===
    void toggleSelectedVisible();
    void cycleSelectedColor();
    void stepSelectedSize(const glm::vec2& delta);

  private:
    [[nodiscard]] FWorkbenchItem* findItem(const std::string& id);
};

} // namespace guiworkbench
