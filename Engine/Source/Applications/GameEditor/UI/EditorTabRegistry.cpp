#include "GameEditor/UI/EditorTabRegistry.h"

#include <algorithm>

namespace ya
{

void EditorTabRegistry::registerTab(FTab tab)
{
    if (tab.id.empty() || !tab.build) {
        return;
    }
    auto it = std::find_if(_tabs.begin(), _tabs.end(), [&](const FTab& current) {
        return current.id == tab.id;
    });
    if (it != _tabs.end()) {
        *it = std::move(tab);
        return;
    }
    _tabs.push_back(std::move(tab));
}

} // namespace ya
