#include "GameEditor/UI/EditorInspectorTab.h"
#include "GameEditor/UI/EditorTransformSection.h"

#include "ECS/ECSRegistry.h"
#include "GUI/Declarative/Build.h"
#include "GUI/Widgets/Controls/Panel.h"
#include "GUI/Widgets/Controls/Text.h"
#include "GUI/Widgets/Controls/TextField.h"
#include "GUI/Widgets/WidgetTree.h"
#include "GameEditor/EditorLayer.h"
#include "Hierarchy/Node.h"
#include "Scene/Core/Scene.h"

#include <algorithm>
#include <format>
#include <ranges>

namespace ya
{

std::shared_ptr<UIElement> EditorInspectorTab::build(WidgetTree&)
{
    auto nameField = ui::textField("InspectorName").setSize({220.0f, 26.0f}).setFontSize(14);
    _nameField = nameField.share();
    _nameField->_onCommit = [this](const std::string& text) {
        if (!_layer) return;
        if (Entity* entity = _layer->getSelectedEntity()) {
            if (Scene* scene = _layer->getHierarchyScene()) {
                if (Node* node = scene->getNodeByEntity(entity)) node->setName(text);
            }
        }
    };

    auto empty = ui::text("InspectorEmpty").setText("No selection").setStyleKey("text.muted");
    _emptyText = empty.share();
    auto entityText = ui::text("InspectorEntityId").setText("Entity ID: -").setFontSize(12).setStyleKey("text.muted");
    _entityText = entityText.share();
    auto componentsText = ui::text("InspectorComponents").setText("Components: -").setFontSize(12).setWrap(true).setStyleKey("text.muted");
    _componentsText = componentsText.share();

    auto form = ui::column("InspectorForm")
                    .fillParent()
                    .setPadding({10.0f, 8.0f})
                    .setSpacing(6.0f)
                    .child(ui::text("InspectorTitle").setText("INSPECTOR").setStyleKey("text.eyebrow"))
                    .child(std::move(entityText))
                    .child(std::move(componentsText))
                    .child(ui::text("NameLabel").setText("Name").setFontSize(12))
                    .child(std::move(nameField))
                    .child(std::move(empty))
                    .child(ui::text("TransformLabel").setText("Transform").setFontSize(12));

    _transformSection = std::make_shared<EditorTransformSection>("InspectorTransform", *_layer);
    form.child(_transformSection);
    return ui::panel("InspectorBody").fillParent().setStyleKey("panel").child(std::move(form)).release();
}

void EditorInspectorTab::sync(WidgetTree& tree)
{
    if (!_layer) return;
    Entity* entity = _layer->getSelectedEntity();
    const bool selected = entity != nullptr;
    if (_emptyText) _emptyText->setVisibility(selected ? EWidgetVisibility::Hidden : EWidgetVisibility::Visible);
    if (_entityText) _entityText->setText(entity ? std::format("Entity ID: {}", entity->getId()) : "Entity ID: -");
    if (_componentsText) {
        std::string summary = "Components: -";
        if (entity && entity->getScene()) {
            std::vector<std::string> names;
            auto& registry = ECSRegistry::get();
            for (const auto& [name, typeIndex] : registry.getTypeIndexCache()) {
                if (registry.getComponent(typeIndex, entity->getScene()->getRegistry(), entity->getHandle())) names.push_back(name.toString());
            }
            std::sort(names.begin(), names.end());
            if (!names.empty()) {
                summary = "Components: " + names.front();
                for (size_t i = 1; i < names.size(); ++i) summary += ", " + names[i];
            }
        }
        _componentsText->setText(summary);
    }
    UIElement* focused = tree.getFocused();
    if (_nameField && focused != _nameField.get() && entity) {
        if (Scene* scene = _layer->getHierarchyScene()) {
            if (Node* node = scene->getNodeByEntity(entity)) _nameField->setText(node->getName());
        }
    }
    if (_transformSection) _transformSection->sync(tree);
}

bool EditorInspectorTab::wantsTextInput(WidgetTree& tree) const
{
    UIElement* focused = tree.getFocused();
    return focused == _nameField.get() || (_transformSection && _transformSection->wantsTextInput(tree));
}

void EditorInspectorTab::reset()
{
    _nameField.reset();
    _entityText.reset();
    _componentsText.reset();
    _emptyText.reset();
    _transformSection.reset();
}

} // namespace ya
