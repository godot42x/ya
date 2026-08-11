#include "GUIWorkbenchWorkspace.h"

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

void FWorkbenchWorkspace::loadMockDocument()
{
    items = {
        FWorkbenchItem{.id = "item.cube",   .name = "Cube",   .bVisible = true,  .color = kColorPalette[0], .size = {140.0f, 90.0f}},
        FWorkbenchItem{.id = "item.sphere", .name = "Sphere", .bVisible = true,  .color = kColorPalette[1], .size = {110.0f, 110.0f}},
        FWorkbenchItem{.id = "item.light",  .name = "Light",  .bVisible = false, .color = kColorPalette[3], .size = {60.0f, 60.0f}},
    };
    selectedId = items.empty() ? "" : items.front().id;
    bDirty     = false;
}

void FWorkbenchWorkspace::newDocument()
{
    items = {FWorkbenchItem{.id = "item.untitled", .name = "Untitled", .bVisible = true,
                            .color = kColorPalette[0], .size = {140.0f, 90.0f}}};
    selectedId    = "item.untitled";
    bDirty        = false;
    commandResult = "New: created 'Untitled'";
}

void FWorkbenchWorkspace::openDocument()
{
    loadMockDocument();
    commandResult = "Open: loaded mock document";
}

void FWorkbenchWorkspace::saveDocument()
{
    if (!bDirty) {
        commandResult = "Save: nothing to save";
        return;
    }
    bDirty        = false;
    commandResult = "Save: saved changes";
}

void FWorkbenchWorkspace::reloadDocument()
{
    loadMockDocument();
    commandResult = "Reload: restored mock document";
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
    if (index < 0 || delta == 0) {
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

void FWorkbenchWorkspace::renameSelected(const std::string& name)
{
    if (FWorkbenchItem* item = getSelectedMutable()) {
        if (item->name != name) {
            item->name = name;
            bDirty     = true;
        }
    }
}

void FWorkbenchWorkspace::toggleSelectedVisible()
{
    if (FWorkbenchItem* item = getSelectedMutable()) {
        item->bVisible = !item->bVisible;
        bDirty         = true;
    }
}

void FWorkbenchWorkspace::cycleSelectedColor()
{
    if (FWorkbenchItem* item = getSelectedMutable()) {
        const auto it = std::find(kColorPalette.begin(), kColorPalette.end(), item->color);
        const size_t next = (it == kColorPalette.end())
                                ? 0
                                : static_cast<size_t>(std::distance(kColorPalette.begin(), it) + 1) %
                                      kColorPalette.size();
        item->color = kColorPalette[next];
        bDirty      = true;
    }
}

void FWorkbenchWorkspace::stepSelectedSize(const glm::vec2& delta)
{
    if (FWorkbenchItem* item = getSelectedMutable()) {
        item->size = glm::max(glm::vec2(20.0f), item->size + delta);
        bDirty     = true;
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
