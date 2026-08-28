#include "GameEditor/UI/EditorTransformSection.h"

#include "GUI/Widgets/WidgetTree.h"
#include "GameEditor/EditorLayer.h"
#include "GameEditor/Inspector/PropertyGraph.h"
#include "GameEditor/Inspector/PropertyProjection.h"
#include "Scene3D/TransformComponent.h"

namespace ya
{

EditorTransformSection::EditorTransformSection(std::string name, EditorLayer& layer)
    : UICompoundWidget(std::move(name), "panel"), _layer(&layer)
{
}

void EditorTransformSection::construct()
{
    Entity* entity = _layer->getSelectedEntity();
    auto* tc = entity ? entity->getComponent<TransformComponent>() : nullptr;
    if (!tc) return;
    _boundEntity = entity;
    _autoSection = createAutoSection(*entity);
    addDetachedChild(_autoSection);
}

std::shared_ptr<EditorAutoPropertySection> EditorTransformSection::createAutoSection(Entity& entity) const
{
    auto* tc = entity.getComponent<TransformComponent>();
    if (!tc) return {};
    PropertyGraph graph = PropertyGraph::build(type_index_v<TransformComponent>, {tc});
    registerBuiltinPropertyProjections();
    PropertyProjectionRegistry::instance().apply(type_index_v<TransformComponent>, graph);
    static constexpr const char* names[3] = {"_position", "_rotation", "_scale"};
    for (int group = 0; group < 3; ++group) {
        if (PropertyNode* node = graph.find(names[group])) {
            PropertyHandle::Vec3Setter setter;
            if (group == 0) setter = [](void* object, const glm::vec3& value) { static_cast<TransformComponent*>(object)->setPosition(value); };
            else if (group == 1) setter = [](void* object, const glm::vec3& value) { static_cast<TransformComponent*>(object)->setRotation(value); };
            else setter = [](void* object, const glm::vec3& value) { static_cast<TransformComponent*>(object)->setScale(value); };
            node->binding = PropertyHandleFactory::make(type_index_v<TransformComponent>, {tc}, node->name, std::move(setter));
        }
    }
    return std::make_shared<EditorAutoPropertySection>("TransformAutoProperties", std::move(graph));
}

void EditorTransformSection::sync(WidgetTree& tree)
{
    Entity* entity = _layer ? _layer->getSelectedEntity() : nullptr;
    if (entity != _boundEntity) {
        if (_autoSection && _autoSection->isAttached()) tree.detach(*_autoSection);
        _autoSection.reset();
        _boundEntity = entity;
        if (entity && entity->getComponent<TransformComponent>()) {
            _autoSection = createAutoSection(*entity);
            tree.attach(*this, _autoSection);
        }
    }
    if (_autoSection) _autoSection->sync(tree);
}

bool EditorTransformSection::wantsTextInput(WidgetTree& tree) const
{
    return _autoSection && _autoSection->wantsTextInput(tree);
}

} // namespace ya
