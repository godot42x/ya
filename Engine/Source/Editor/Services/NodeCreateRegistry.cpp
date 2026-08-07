#include "Editor/Services/NodeCreateRegistry.h"

#include "Scene/Node.h"
#include "UI/Scene/Node2D.h"
#include "Scene/Scene.h"

#include <algorithm>

namespace ya::editor
{

NodeCreateRegistry& NodeCreateRegistry::get()
{
    static NodeCreateRegistry registry;
    return registry;
}

void NodeCreateRegistry::registerPreset(std::string category,
                                        std::string displayName,
                                        std::string doc,
                                        std::function<Node*(Scene&, const std::string&, Node*)> factory)
{
    _presets.push_back(NodeCreateEntry{
        .category    = std::move(category),
        .displayName = std::move(displayName),
        .doc         = std::move(doc),
        .factory     = std::move(factory),
    });
}

std::vector<NodeCreateEntry> NodeCreateRegistry::uiEntries() const
{
    std::vector<NodeCreateEntry> entries;
    for (const std::string& typeName : getRegisteredUINodeTypeNames()) {
        std::string displayName = typeName;
        // "UICanvasNode" -> "Canvas", "UIPanelNode" -> "Panel", ...
        if (displayName.starts_with("UI") && displayName.ends_with("Node")) {
            displayName = displayName.substr(2, displayName.size() - 2 - 4);
        }
        entries.push_back(NodeCreateEntry{
            .category    = "2D",
            .displayName = std::move(displayName),
            .doc         = "Create a " + typeName + " UI node",
            .factory     = [typeName](Scene& scene, const std::string& name, Node* parent) -> Node* {
                return scene.createUINode(typeName, name, parent);
            },
        });
    }
    return entries;
}

Node* NodeCreateRegistry::createPreset(const std::string& displayName,
                                       Scene&              scene,
                                       const std::string&  name,
                                       Node*               parent) const
{
    for (const NodeCreateEntry& entry : _presets) {
        if (entry.displayName == displayName) {
            return entry.factory(scene, name, parent);
        }
    }
    return nullptr;
}

} // namespace ya::editor
