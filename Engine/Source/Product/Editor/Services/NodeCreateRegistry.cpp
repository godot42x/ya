#include "Editor/Services/NodeCreateRegistry.h"

#include "Hierarchy/Node.h"
#include "Scene/Core/Scene.h"

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
