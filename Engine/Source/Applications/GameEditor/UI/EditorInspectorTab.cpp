#include "GameEditor/UI/EditorInspectorTab.h"

#include "ECS/ECSRegistry.h"
#include "GUI/Declarative/Build.h"
#include "GUI/Widgets/Controls/InputExtras.h"
#include "GUI/Widgets/Controls/Panel.h"
#include "GUI/Widgets/Controls/Text.h"
#include "GUI/Widgets/Controls/TextField.h"
#include "GUI/Widgets/WidgetTree.h"
#include "GameEditor/EditorLayer.h"
#include "Hierarchy/Node.h"
#include "Scene/Core/Scene.h"
#include "Scene3D/TransformComponent.h"

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

    const char* names[9] = {"PosX", "PosY", "PosZ", "RotX", "RotY", "RotZ", "SclX", "SclY", "SclZ"};
    const float speeds[9] = {0.1f, 0.1f, 0.1f, 0.5f, 0.5f, 0.5f, 0.01f, 0.01f, 0.01f};
    auto rows = ui::column("TransformRows").setSpacing(4.0f);
    for (int group = 0; group < 3; ++group) {
        auto row = ui::row(std::format("TransformRow{}", group)).setSpacing(4.0f);
        for (int axis = 0; axis < 3; ++axis) {
            const int index = group * 3 + axis;
            auto drag = std::make_shared<UIDragFloat>(names[index]);
            drag->setSize({72.0f, 22.0f});
            drag->_speed = speeds[index];
            drag->_onValueChanged = [this, index](float value) {
                if (!_layer) return;
                Entity* entity = _layer->getSelectedEntity();
                auto* tc = entity ? entity->getComponent<TransformComponent>() : nullptr;
                if (!tc) return;
                auto pos = tc->getPosition();
                auto rot = tc->getRotation();
                auto scl = tc->getScale();
                if (index < 3) { pos[index] = value; tc->setPosition(pos); }
                else if (index < 6) { rot[index - 3] = value; tc->setRotation(rot); }
                else { scl[index - 6] = value; tc->setScale(scl); }
            };
            _transformDrags[static_cast<size_t>(index)] = drag;
            row.child(drag);
        }
        rows.child(std::move(row));
    }
    form.child(std::move(rows));
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
    auto* tc = entity ? entity->getComponent<TransformComponent>() : nullptr;
    if (!tc) return;
    const float values[9] = {tc->getPosition().x, tc->getPosition().y, tc->getPosition().z,
                             tc->getRotation().x, tc->getRotation().y, tc->getRotation().z,
                             tc->getScale().x, tc->getScale().y, tc->getScale().z};
    for (size_t i = 0; i < _transformDrags.size(); ++i) {
        if (_transformDrags[i] && focused != _transformDrags[i].get()) _transformDrags[i]->setValue(values[i]);
    }
}

bool EditorInspectorTab::wantsTextInput(WidgetTree& tree) const
{
    UIElement* focused = tree.getFocused();
    return focused == _nameField.get() ||
           std::ranges::any_of(_transformDrags, [focused](const auto& drag) { return focused == drag.get(); });
}

void EditorInspectorTab::reset()
{
    _nameField.reset();
    _entityText.reset();
    _componentsText.reset();
    _emptyText.reset();
    _transformDrags = {};
}

} // namespace ya
