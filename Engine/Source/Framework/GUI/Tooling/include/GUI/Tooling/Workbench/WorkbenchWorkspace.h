#pragma once

#include "Core/Api.h"

#include <glm/glm.hpp>

#include <string>
#include <vector>

namespace guiworkbench
{

struct FWorkbenchItem
{
    std::string id;
    std::string name;
    /// Parent item id; empty = root. The tree is flat-stored (children of a
    /// removed item are reparented to their grandparent).
    std::string parentId;
    bool        bVisible = true;
    glm::vec4   color    = {1.0f, 1.0f, 1.0f, 1.0f};
    glm::vec2   size     = {140.0f, 90.0f};
};

struct YA_GUI_API FWorkbenchWorkspace
{
    std::vector<FWorkbenchItem> items;
    std::string                 selectedId;
    bool                        bDirty        = false;
    std::string                 commandResult = "Ready";

    void addItem(const std::string& name = "Item");
    void removeSelected();
    void renameSelected(const std::string& name);
    void resetLayout();

    void select(const std::string& id);
    void selectRelative(int delta);
    [[nodiscard]] const FWorkbenchItem* getSelected() const;
    [[nodiscard]] FWorkbenchItem*       getSelectedMutable();
    [[nodiscard]] int                   getSelectedIndex() const;

    void toggleSelectedVisible();
    void cycleSelectedColor();
    void stepSelectedSize(const glm::vec2& delta);
    /// Move `id` under `newParentId` ("" = root). Rejects self-parenting,
    /// unknown parents and cycles (newParent must not be a descendant of id).
    bool reparent(const std::string& id, const std::string& newParentId);
    /// Depth of `id` in the tree (0 = root).
    [[nodiscard]] int getDepth(const std::string& id) const;
    /// Items in tree order (parents before children, stable sibling order)
    /// — the list presenter uses this; selection stays id-based.
    [[nodiscard]] std::vector<const FWorkbenchItem*> orderedItems() const;

  private:
    [[nodiscard]] FWorkbenchItem* findItem(const std::string& id);
    void collectOrdered(std::vector<const FWorkbenchItem*>& out, const std::string& parentId) const;
};

} // namespace guiworkbench
