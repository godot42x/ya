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
    const std::string removedId = items[static_cast<size_t>(index)].id;
    const std::string name      = items[static_cast<size_t>(index)].name;
    const std::string removedParent = items[static_cast<size_t>(index)].parentId;
    items.erase(items.begin() + index);
    // Children of the removed item move up to its parent slot.
    for (FWorkbenchItem& item : items) {
        if (item.parentId == removedId) {
            item.parentId = removedParent;
        }
    }
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
    const auto ordered = orderedItems();
    for (size_t i = 0; i < ordered.size(); ++i) {
        if (ordered[i]->id == selectedId) {
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

void FWorkbenchWorkspace::collectOrdered(std::vector<const FWorkbenchItem*>& out,
                                         const std::string& parentId) const
{
    for (const FWorkbenchItem& item : items) {
        if (item.parentId == parentId) {
            out.push_back(&item);
            collectOrdered(out, item.id);
        }
    }
}

std::vector<const FWorkbenchItem*> FWorkbenchWorkspace::orderedItems() const
{
    std::vector<const FWorkbenchItem*> out;
    collectOrdered(out, "");
    return out;
}

int FWorkbenchWorkspace::getDepth(const std::string& id) const
{
    const auto findById = [this](const std::string& target) -> const FWorkbenchItem*
    {
        for (const FWorkbenchItem& item : items) {
            if (item.id == target) {
                return &item;
            }
        }
        return nullptr;
    };

    int depth = 0;
    const FWorkbenchItem* start = findById(id);
    for (std::string parent = start ? start->parentId : ""; !parent.empty();) {
        ++depth;
        const FWorkbenchItem* parentItem = findById(parent);
        if (!parentItem) {
            break;
        }
        parent = parentItem->parentId;
        if (depth > static_cast<int>(items.size())) {
            break; // cycle guard (should be unreachable; reparent prevents it)
        }
    }
    return depth;
}

bool FWorkbenchWorkspace::reparent(const std::string& id, const std::string& newParentId)
{
    if (id.empty() || id == newParentId) {
        return false;
    }
    FWorkbenchItem* item = findItem(id);
    if (!item) {
        return false;
    }
    if (!newParentId.empty() && !findItem(newParentId)) {
        return false;
    }
    // Reject cycles: the new parent must not be a descendant of `id`.
    for (std::string cursor = newParentId; !cursor.empty();) {
        if (cursor == id) {
            return false;
        }
        const FWorkbenchItem* parent = findItem(cursor);
        if (!parent) {
            break;
        }
        cursor = parent->parentId;
    }
    if (item->parentId == newParentId) {
        return true; // no-op, already there
    }
    item->parentId = newParentId;
    bDirty         = true;
    commandResult  = "Reparent: '" + item->name + "'";
    return true;
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
