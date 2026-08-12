#pragma once

#include <glm/glm.hpp>

#include <string>
#include <vector>

namespace guiworkbench
{

struct FWorkbenchItem
{
    std::string id;
    std::string name;
    bool        bVisible = true;
    glm::vec4   color    = {1.0f, 1.0f, 1.0f, 1.0f};
    glm::vec2   size     = {140.0f, 90.0f};
};

struct FWorkbenchWorkspace
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

  private:
    [[nodiscard]] FWorkbenchItem* findItem(const std::string& id);
};

} // namespace guiworkbench
