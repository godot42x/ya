#include "GUI/Binding/SelectionModel.h"

#include <algorithm>

namespace ya
{

SelectionModel::SelectionModel()
    : _primary(std::make_shared<Reactive<std::string>>())
    , _hovered(std::make_shared<Reactive<std::string>>())
    , _active(std::make_shared<Reactive<std::string>>())
    , _focused(std::make_shared<Reactive<std::string>>())
{
}

void SelectionModel::bump()
{
    ++_revision;
}

void SelectionModel::setPrimary(std::string id)
{
    _primary->set(std::move(id));
}

void SelectionModel::restorePrimaryAfterRemove(const std::string& removed)
{
    if (_primary->value() != removed) {
        return;
    }
    setPrimary(_selected.empty() ? std::string{} : _selected.back());
}

bool SelectionModel::contains(const std::string& id) const
{
    return !id.empty() && _index.contains(id);
}

void SelectionModel::replace(std::vector<std::string> ids, std::string primary)
{
    std::vector<std::string> next;
    std::unordered_set<std::string> index;
    next.reserve(ids.size());
    for (std::string& id : ids) {
        if (id.empty() || index.contains(id)) {
            continue;
        }
        index.insert(id);
        next.push_back(std::move(id));
    }
    if (primary.empty()) {
        primary = next.empty() ? std::string{} : next.front();
    }
    else if (!index.contains(primary)) {
        index.insert(primary);
        next.insert(next.begin(), primary);
    }
    else if (next.front() != primary) {
        next.erase(std::find(next.begin(), next.end(), primary));
        next.insert(next.begin(), primary);
    }
    if (next == _selected && _primary->value() == primary) {
        return;
    }
    _selected = std::move(next);
    _index    = std::move(index);
    setPrimary(std::move(primary));
    bump();
}

void SelectionModel::select(std::string id)
{
    if (id.empty()) {
        clear();
        return;
    }
    if (_selected.size() == 1 && _selected.front() == id && _primary->value() == id) {
        return;
    }
    _selected = {id};
    _index    = {id};
    setPrimary(std::move(id));
    bump();
}

void SelectionModel::add(std::string id)
{
    if (id.empty()) {
        return;
    }
    if (!contains(id)) {
        _index.insert(id);
        _selected.push_back(id);
        bump();
    }
    setPrimary(std::move(id));
}

void SelectionModel::toggle(std::string id)
{
    if (id.empty()) {
        return;
    }
    if (contains(id)) {
        remove(id);
        return;
    }
    add(std::move(id));
}

void SelectionModel::remove(const std::string& id)
{
    if (!contains(id)) {
        return;
    }
    _index.erase(id);
    _selected.erase(std::remove(_selected.begin(), _selected.end(), id), _selected.end());
    restorePrimaryAfterRemove(id);
    bump();
}

void SelectionModel::clear()
{
    if (_selected.empty() && _primary->value().empty()) {
        return;
    }
    _selected.clear();
    _index.clear();
    setPrimary({});
    bump();
}

void SelectionModel::clearTransient()
{
    const bool bChanged = !_hovered->value().empty() || !_active->value().empty() || !_focused->value().empty();
    _hovered->set({});
    _active->set({});
    _focused->set({});
    if (bChanged) {
        bump();
    }
}

void SelectionModel::setHovered(std::string id)
{
    _hovered->set(std::move(id));
}

void SelectionModel::setActive(std::string id)
{
    _active->set(std::move(id));
}

void SelectionModel::setFocused(std::string id)
{
    _focused->set(std::move(id));
}

} // namespace ya
