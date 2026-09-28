#include "GameEditor/UI/Ops/EditorCreateMenu.h"

#include "GameEditor/EditorLayer.h"
#include "GameEditor/Services/NodeCreateRegistry.h"

#include <algorithm>
#include <format>
#include <string>

namespace ya
{

std::vector<UIMenu::FItem> makeEditorCreateMenuItems(EditorLayer& layer, Node* parent)
{
    const bool bCanAuthor = layer.canViewportAuthor();

    std::vector<UIMenu::FItem> items;
    items.push_back({
        .label    = "Create Empty Node",
        .action   = [&layer, parent]() { layer.cmdCreateEmptyNode(parent); },
        .bEnabled = bCanAuthor,
    });

    std::vector<std::string> categories;
    for (const editor::NodeCreateEntry& entry : editor::NodeCreateRegistry::get().presets()) {
        if (std::find(categories.begin(), categories.end(), entry.category) == categories.end()) {
            categories.push_back(entry.category);
        }
    }

    for (const std::string& category : categories) {
        items.push_back({
            .label          = std::format("Create {}", category),
            .submenuFactory = [&layer, parent, category]() {
                std::vector<UIMenu::FItem> presets;
                for (const editor::NodeCreateEntry& entry : editor::NodeCreateRegistry::get().presets()) {
                    if (entry.category != category) {
                        continue;
                    }
                    const std::string presetName = entry.displayName;
                    presets.push_back({
                        .label  = presetName,
                        .action = [&layer, parent, presetName]() { layer.cmdCreateNodePreset(presetName, parent); },
                    });
                }
                return UIMenu::create(std::move(presets));
            },
            .bEnabled = bCanAuthor,
        });
    }
    return items;
}

} // namespace ya
