#include "GameEditor/UI/Ops/EditorCreateMenu.h"

#include "GameEditor/EditorLayer.h"
#include "GameEditor/Services/NodeCreateRegistry.h"

#include <format>

namespace ya
{

std::vector<UIMenu::FItem> makeEditorCreateMenuItems(EditorLayer& layer, ActionMap& actions)
{
    std::vector<UIMenu::FItem> items;
    items.push_back(UIMenu::FItem::fromAction(actions, "selection.createEmpty"));
    items.push_back({
        .label          = "Create 3D Object",
        .submenuFactory = [&layer]() {
            std::vector<UIMenu::FItem> presets;
            for (const editor::NodeCreateEntry& entry : editor::NodeCreateRegistry::get().presets()) {
                if (entry.category != "3D Object") {
                    continue;
                }
                const std::string presetName = entry.displayName;
                presets.push_back({
                    .label  = presetName,
                    .action = [&layer, presetName]() { layer.cmdCreateNodePreset(presetName); },
                });
            }
            return UIMenu::create(std::move(presets));
        },
    });

    for (const editor::NodeCreateEntry& entry : editor::NodeCreateRegistry::get().presets()) {
        if (entry.category != "Light") {
            continue;
        }
        const std::string presetName = entry.displayName;
        items.push_back({
            .label  = std::format("Create {}", presetName),
            .action = [&layer, presetName]() { layer.cmdCreateNodePreset(presetName); },
        });
    }
    return items;
}

} // namespace ya
