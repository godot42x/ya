#pragma once

// ============================================================================
// FWorkbenchWorkspace - the tool GUI's fact source (gui-app-bootstrap
// Phase 3).
//
// Ownership rule: document, selection and command results live here; widgets
// only hold presentation state and report actions (row selection /
// activation, text edits, button commands). The workspace deliberately does
// not include UIElement/WidgetTree headers: it is testable without the GUI
// closure and is the candidate for later extraction once a second consumer
// (UIDesigner pilot) proves the contract.
// ============================================================================

#include <glm/glm.hpp>

#include <string>
#include <vector>

namespace guiworkbench
{

/// One mock document item.
struct FWorkbenchItem
{
    std::string id;
    std::string name;
    bool        bVisible = true;
    glm::vec4   color    = {1.0f, 1.0f, 1.0f, 1.0f};
    glm::vec2   size     = {140.0f, 90.0f};
};

/// ToolWorkspace: mock document + selection + command state.
struct FWorkbenchWorkspace
{
    std::vector<FWorkbenchItem> items;
    std::string                 selectedId;
    bool                        bDirty        = false;
    std::string                 commandResult = "Ready";

    // === Document commands (stub behavior with visible feedback) ===
    void newDocument();
    void openDocument();
    void saveDocument();
    void reloadDocument();

    // === Selection ===
    void select(const std::string& id);
    /// Move selection by `delta` rows (clamped at both ends; no-op when
    /// nothing is selected).
    void selectRelative(int delta);
    [[nodiscard]] const FWorkbenchItem* getSelected() const;
    [[nodiscard]] FWorkbenchItem*       getSelectedMutable();
    [[nodiscard]] int                   getSelectedIndex() const;

    // === Inspector mutations (mark the document dirty) ===
    void renameSelected(const std::string& name);
    void toggleSelectedVisible();
    void cycleSelectedColor();
    void stepSelectedSize(const glm::vec2& delta);

  private:
    void loadMockDocument();
    [[nodiscard]] FWorkbenchItem* findItem(const std::string& id);
};

} // namespace guiworkbench
