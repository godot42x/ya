#include "GUI/Tooling/Workbench/WorkbenchWorkspace.h"

#include <algorithm>
#include <array>

namespace guiworkbench
{

namespace
{

constexpr std::array<glm::vec4, 4> kColorPalette{
    glm::vec4{0.35f, 0.55f, 0.90f, 1.0f},
    glm::vec4{0.90f, 0.45f, 0.35f, 1.0f},
    glm::vec4{0.40f, 0.85f, 0.50f, 1.0f},
    glm::vec4{0.95f, 0.85f, 0.35f, 1.0f},
};

} // namespace

void FWorkbenchWorkspace::resetLayout()
{
    items = {
        FWorkbenchItem{.id = "item.cube", .name = "Cube", .bVisible = true, .color = kColorPalette[0], .size = {140.0f, 90.0f}},
        FWorkbenchItem{.id = "item.sphere", .name = "Sphere", .bVisible = true, .color = kColorPalette[1], .size = {110.0f, 110.0f}},
        FWorkbenchItem{.id = "item.light", .name = "Light", .bVisible = false, .color = kColorPalette[3], .size = {60.0f, 60.0f}},
    };
    selectedId    = items.empty() ? "" : items.front().id;
    bDirty        = false;
    commandResult = "Reset: layout restored";
}

void FWorkbenchWorkspace::addItem(const std::string& name)
{
    const size_t index = items.size();
    items.push_back(FWorkbenchItem{
        .id       = "item." + std::to_string(index + 1),
        .name     = name,
        .bVisible = true,
        .color    = kColorPalette[static_cast<size_t>(index) % kColorPalette.size()],
        .size     = {120.0f, 80.0f},
    });
    selectedId    = items.back().id;
    bDirty        = true;
    commandResult = "Add: '" + name + "'";
}

void FWorkbenchWorkspace::removeSelected()
{
    if (selectedId.empty()) {
        commandResult = "Remove: nothing selected";
        return;
    }
    const int index = getSelectedIndex();
    if (index < 0) {
        commandResult = "Remove: nothing selected";
        return;
    }
    const std::string name = items[static_cast<size_t>(index)].name;
    items.erase(items.begin() + index);
    if (items.empty()) {
        selectedId.clear();
    } else {
        selectedId = items[static_cast<size_t>(std::min(index, static_cast<int>(items.size()) - 1))].id;
    }
    bDirty        = true;
    commandResult = "Remove: '" + name + "'";
}

void FWorkbenchWorkspace::renameSelected(const std::string& name)
{
    if (FWorkbenchItem* item = getSelectedMutable()) {
        if (item->name != name) {
            item->name = name;
            bDirty     = true;
        }
        commandResult = "Rename: '" + name + "'";
    } else {
        commandResult = "Rename: nothing selected";
    }
}

void FWorkbenchWorkspace::select(const std::string& id)
{
    if (id.empty() || !findItem(id)) {
        return;
    }
    selectedId = id;
}

void FWorkbenchWorkspace::selectRelative(int delta)
{
    const int index = getSelectedIndex();
    if (index < 0 || delta == 0 || items.empty()) {
        return;
    }
    const int next = std::clamp(index + delta, 0, static_cast<int>(items.size()) - 1);
    if (next != index) {
        selectedId = items[static_cast<size_t>(next)].id;
    }
}

const FWorkbenchItem* FWorkbenchWorkspace::getSelected() const
{
    if (selectedId.empty()) {
        return nullptr;
    }
    for (const auto& item : items) {
        if (item.id == selectedId) {
            return &item;
        }
    }
    return nullptr;
}

FWorkbenchItem* FWorkbenchWorkspace::getSelectedMutable()
{
    return findItem(selectedId);
}

int FWorkbenchWorkspace::getSelectedIndex() const
{
    for (size_t i = 0; i < items.size(); ++i) {
        if (items[i].id == selectedId) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

void FWorkbenchWorkspace::toggleSelectedVisible()
{
    if (FWorkbenchItem* item = getSelectedMutable()) {
        item->bVisible = !item->bVisible;
        bDirty         = true;
        commandResult  = "Inspector: visibility toggled";
    } else {
        commandResult = "Inspector: no selection";
    }
}

void FWorkbenchWorkspace::cycleSelectedColor()
{
    if (FWorkbenchItem* item = getSelectedMutable()) {
        const auto   it   = std::find(kColorPalette.begin(), kColorPalette.end(), item->color);
        const size_t next = (it == kColorPalette.end())
                                ? 0
                                : static_cast<size_t>(std::distance(kColorPalette.begin(), it) + 1) %
                                      kColorPalette.size();
        item->color   = kColorPalette[next];
        bDirty        = true;
        commandResult = "Inspector: color changed";
    } else {
        commandResult = "Inspector: no selection";
    }
}

void FWorkbenchWorkspace::stepSelectedSize(const glm::vec2& delta)
{
    if (FWorkbenchItem* item = getSelectedMutable()) {
        item->size     = glm::max(glm::vec2(20.0f), item->size + delta);
        bDirty         = true;
        commandResult  = "Inspector: size changed";
    } else {
        commandResult = "Inspector: no selection";
    }
}

FWorkbenchItem* FWorkbenchWorkspace::findItem(const std::string& id)
{
    for (auto& item : items) {
        if (item.id == id) {
            return &item;
        }
    }
    return nullptr;
}

} // namespace guiworkbench
