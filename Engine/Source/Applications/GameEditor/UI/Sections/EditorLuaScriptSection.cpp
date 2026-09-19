#include "GameEditor/UI/Sections/EditorLuaScriptSection.h"

#include "ECS/Component.h"
#include "ECS/Entity.h"
#include "ECS/Systems/Components/LuaScriptComponent.h"
#include "GUI/Declarative/Build.h"
#include "GUI/Layout/UILayout.h"
#include "GUI/Widgets/Controls/Button.h"
#include "GUI/Widgets/Controls/CheckBox.h"
#include "GUI/Widgets/Controls/Container.h"
#include "GUI/Widgets/Controls/DragFloat.h"
#include "GUI/Widgets/Controls/Expander.h"
#include "GUI/Widgets/Controls/Text.h"
#include "GUI/Widgets/Controls/TextField.h"
#include "GUI/Widgets/UIElement.h"
#include "GUI/Widgets/WidgetTree.h"
#include "GameEditor/EditorLayer.h"
#include "GameEditor/UI/Shell/EditorListRows.h"
#include "GameEditor/UI/Shell/EditorTheme.h"
#include "Scene/Core/Scene.h"

#include <any>
#include <format>
#include <glm/glm.hpp>
#include <type_traits>

namespace ya
{

namespace
{

LuaScriptComponent* scriptComponentOn(EditorLayer* layer, uint64_t uuid)
{
    if (!layer || uuid == 0) {
        return nullptr;
    }
    Scene* scene = layer->getHierarchyScene();
    if (!scene) {
        return nullptr;
    }
    Entity* entity = scene->getEntityByUUID(uuid);
    if (!entity || !entity->isValid()) {
        return nullptr;
    }
    return entity->tryGetComponent<LuaScriptComponent>();
}

void storeOverride(LuaScriptComponent::ScriptInstance& script,
                   LuaScriptComponent::ScriptProperty& prop,
                   std::any value)
{
    script.propertyOverrides[prop.name] = value;
    prop.value = std::move(value);
}

template <typename T>
T anyAs(const std::any& value, T fallback = T{})
{
    if (value.type() == typeid(T)) {
        return std::any_cast<T>(value);
    }
    if constexpr (std::is_same_v<T, float>) {
        if (value.type() == typeid(double)) {
            return static_cast<float>(std::any_cast<double>(value));
        }
        if (value.type() == typeid(int)) {
            return static_cast<float>(std::any_cast<int>(value));
        }
    }
    if constexpr (std::is_same_v<T, int>) {
        if (value.type() == typeid(float)) {
            return static_cast<int>(std::any_cast<float>(value));
        }
        if (value.type() == typeid(double)) {
            return static_cast<int>(std::any_cast<double>(value));
        }
    }
    return fallback;
}

} // namespace

EditorLuaScriptSection::EditorLuaScriptSection(std::string name,
                                               EditorLayer& layer,
                                               uint64_t entityUuid,
                                               std::function<void()> onMutated,
                                               ScriptPicker picker)
    : UICompoundWidget(std::move(name), "panel")
    , _layer(&layer)
    , _entityUuid(entityUuid)
    , _onMutated(std::move(onMutated))
    , _picker(std::move(picker))
{
    enableTick();
}

EditorLuaScriptSection::~EditorLuaScriptSection()
{
    releasePreviewHandles();
}

void EditorLuaScriptSection::releasePreviewHandles()
{
    auto releaseOnScene = [this](Scene* scene) {
        if (!scene || _entityUuid == 0) {
            return;
        }
        Entity* entity = scene->getEntityByUUID(_entityUuid);
        if (!entity || !entity->isValid()) {
            return;
        }
        if (LuaScriptComponent* lsc = entity->tryGetComponent<LuaScriptComponent>()) {
            _preview.releaseMatching(*lsc);
        }
    };
    if (_layer) {
        releaseOnScene(_layer->getHierarchyScene());
        releaseOnScene(_layer->getEditableScene());
    }
}

void EditorLuaScriptSection::noteMutated()
{
    if (_onMutated) {
        _onMutated();
    }
}

void EditorLuaScriptSection::construct()
{
    auto addButton = labeledButton("LuaScriptAdd", "+ Add Script");
    addButton.setOnClick([this]() {
        LuaScriptComponent* lsc = scriptComponentOn(_layer, _entityUuid);
        if (!lsc) {
            return;
        }
        if (!_picker) {
            lsc->addScript("");
            noteMutated();
            _fingerprint.clear();
            return;
        }
        _picker("", [this](std::string path) {
            LuaScriptComponent* target = scriptComponentOn(_layer, _entityUuid);
            if (!target) {
                return;
            }
            target->addScript(std::move(path));
            noteMutated();
            _fingerprint.clear();
        });
    });

    _rows = ui::column("LuaScriptRows").setSpacing(editor_density::kRowSpacing).share();
    addDetachedChild(ui::column("LuaScriptRoot")
                         .setSpacing(editor_density::kRowSpacing)
                         .child(std::move(addButton),
                                ui::boxSlot().preferredSize({0.0f, editor_density::kToolbarHeight}))
                         .child(_rows)
                         .release());
}

std::string EditorLuaScriptSection::fingerprint() const
{
    LuaScriptComponent* lsc = scriptComponentOn(_layer, _entityUuid);
    if (!lsc) {
        return {};
    }
    std::string out = std::to_string(lsc->scripts.size());
    for (const auto& script : lsc->scripts) {
        out += '|';
        out += script.scriptPath;
        out += script.enabled ? "1" : "0";
        out += script.bLoaded ? "R" : (script.bAuthoringPreviewLoaded ? "P" : (script.bAuthoringPreviewAttempted ? "F" : "N"));
        out += std::to_string(script.properties.size());
    }
    return out;
}

void EditorLuaScriptSection::rebuildRows()
{
    if (!_rows) {
        return;
    }
    WidgetTree* tree = getTree();
    LuaScriptComponent* lsc = scriptComponentOn(_layer, _entityUuid);
    if (tree) {
        for (const auto& child : _rowWidgets) {
            if (child && child->isAttached()) {
                tree->detach(*child);
            }
        }
    }
    _rowWidgets.clear();
    if (!lsc) {
        return;
    }

    for (size_t i = 0; i < lsc->scripts.size(); ++i) {
        auto& script = lsc->scripts[i];
        const bool bAuthoringScene =
            _layer && _layer->getHierarchyScene() != nullptr &&
            _layer->getHierarchyScene() == _layer->getEditableScene();
        if (bAuthoringScene && !script.scriptPath.empty() && !script.bLoaded &&
            !script.bAuthoringPreviewAttempted) {
            _preview.load(script);
        }

        const std::string key = std::format("LuaScript_{}", i);
        auto pathField = ui::textField(key + "_Path")
                             .setStyleKey(editorStyle(StyleKey::TextField))
                             .setText(script.scriptPath);
        pathField.setOnCommit([this, i](const std::string& text) {
            LuaScriptComponent* target = scriptComponentOn(_layer, _entityUuid);
            if (!target || i >= target->scripts.size()) {
                return;
            }
            auto& row = target->scripts[i];
            row.scriptPath = LuaScriptComponent::ScriptInstance::normalizeScriptPath(text);
            row.releaseLuaHandles();
            row.bLoaded = false;
            row.bAuthoringPreviewAttempted = false;
            row.bAuthoringPreviewLoaded = false;
            row.properties.clear();
            noteMutated();
            _fingerprint.clear();
        });

        auto browse = labeledButton(key + "_Browse", "Browse");
        browse.setOnClick([this, i]() {
            LuaScriptComponent* target = scriptComponentOn(_layer, _entityUuid);
            if (!target || i >= target->scripts.size() || !_picker) {
                return;
            }
            _picker(target->scripts[i].scriptPath, [this, i](std::string path) {
                LuaScriptComponent* inner = scriptComponentOn(_layer, _entityUuid);
                if (!inner || i >= inner->scripts.size()) {
                    return;
                }
                inner->scripts[i].scriptPath =
                    LuaScriptComponent::ScriptInstance::normalizeScriptPath(path);
                inner->scripts[i].releaseLuaHandles();
                inner->scripts[i].bLoaded = false;
                inner->scripts[i].bAuthoringPreviewAttempted = false;
                inner->scripts[i].bAuthoringPreviewLoaded = false;
                inner->scripts[i].properties.clear();
                noteMutated();
                _fingerprint.clear();
            });
        });

        auto enabled = ui::checkBox(key + "_Enabled").setText("Enabled").setChecked(script.enabled);
        enabled.setOnChanged([this, i](bool checked) {
            LuaScriptComponent* target = scriptComponentOn(_layer, _entityUuid);
            if (!target || i >= target->scripts.size()) {
                return;
            }
            target->scripts[i].enabled = checked;
            noteMutated();
        });

        const char* status = "Status: Not Loaded";
        std::string statusStyle = "text.muted";
        if (script.bLoaded) {
            status = "Status: Loaded (Runtime)";
        }
        else if (script.bAuthoringPreviewLoaded) {
            status      = "Status: Preview Mode (Editor)";
            statusStyle = "text";
        }
        else if (!script.scriptPath.empty() && script.bAuthoringPreviewAttempted) {
            status      = "Status: Failed to load";
            statusStyle = "text.error";
        }

        auto props = ui::column(key + "_Props").setSpacing(editor_density::kRowSpacing);
        if (script.bLoaded || script.bAuthoringPreviewLoaded) {
            if (script.properties.empty()) {
                props.child(ui::text(key + "_NoProps")
                                .setText("No _PROPERTIES table on this script")
                                .setStyleKey("text.muted")
                                .setWrap(true));
            }
            else {
                for (size_t p = 0; p < script.properties.size(); ++p) {
                    auto& prop = script.properties[p];
                    auto row = ui::row(std::format("{}_Prop{}", key, p))
                                   .setSpacing(editor_density::kControlSpacing);
                    row.child(ui::text(std::format("{}_Prop{}Label", key, p))
                                  .setText(prop.name)
                                  .setStyleKey("text.muted")
                                  .setVAlign(EWidgetAlignV::Center),
                              ui::boxSlot().preferredSize({editor_density::kLabelColumn,
                                                           editor_density::kRowHeight}));
                    if (prop.typeHint == "float") {
                        auto drag = std::make_shared<UIDragFloat>(std::format("{}_Prop{}Value", key, p));
                        drag->setStyleKey(editorStyle(StyleKey::DragFloat));
                        if (prop.value.has_value()) {
                            drag->setValue(anyAs<float>(prop.value), false);
                        }
                        drag->_min = prop.min;
                        drag->_max = prop.max;
                        drag->_onValueChanged = [this, i, p](float value) {
                            LuaScriptComponent* target = scriptComponentOn(_layer, _entityUuid);
                            if (!target || i >= target->scripts.size() ||
                                p >= target->scripts[i].properties.size()) {
                                return;
                            }
                            storeOverride(target->scripts[i], target->scripts[i].properties[p], value);
                            noteMutated();
                        };
                        row.child(drag, ui::boxSlot().fillWidth().preferredSize({0.0f, editor_density::kRowHeight}));
                    }
                    else if (prop.typeHint == "int") {
                        auto drag = std::make_shared<UIDragFloat>(std::format("{}_Prop{}Value", key, p));
                        drag->setStyleKey(editorStyle(StyleKey::DragFloat));
                        drag->_decimals = 0;
                        drag->_speed = 1.0f;
                        if (prop.value.has_value()) {
                            drag->setValue(static_cast<float>(anyAs<int>(prop.value)), false);
                        }
                        drag->_min = prop.min;
                        drag->_max = prop.max;
                        drag->_onValueChanged = [this, i, p](float value) {
                            LuaScriptComponent* target = scriptComponentOn(_layer, _entityUuid);
                            if (!target || i >= target->scripts.size() ||
                                p >= target->scripts[i].properties.size()) {
                                return;
                            }
                            storeOverride(target->scripts[i],
                                          target->scripts[i].properties[p],
                                          static_cast<int>(value));
                            noteMutated();
                        };
                        row.child(drag, ui::boxSlot().fillWidth().preferredSize({0.0f, editor_density::kRowHeight}));
                    }
                    else if (prop.typeHint == "bool") {
                        bool checked = anyAs<bool>(prop.value);
                        auto box = ui::checkBox(std::format("{}_Prop{}Value", key, p)).setChecked(checked);
                        box.setOnChanged([this, i, p](bool value) {
                            LuaScriptComponent* target = scriptComponentOn(_layer, _entityUuid);
                            if (!target || i >= target->scripts.size() ||
                                p >= target->scripts[i].properties.size()) {
                                return;
                            }
                            storeOverride(target->scripts[i], target->scripts[i].properties[p], value);
                            noteMutated();
                        });
                        row.child(std::move(box),
                                  ui::boxSlot().fillWidth().preferredSize({0.0f, editor_density::kRowHeight}));
                    }
                    else if (prop.typeHint == "string") {
                        std::string text = anyAs<std::string>(prop.value);
                        auto field = ui::textField(std::format("{}_Prop{}Value", key, p))
                                         .setStyleKey(editorStyle(StyleKey::TextField))
                                         .setText(text);
                        field.setOnCommit([this, i, p](const std::string& value) {
                            LuaScriptComponent* target = scriptComponentOn(_layer, _entityUuid);
                            if (!target || i >= target->scripts.size() ||
                                p >= target->scripts[i].properties.size()) {
                                return;
                            }
                            storeOverride(target->scripts[i], target->scripts[i].properties[p], value);
                            noteMutated();
                        });
                        row.child(std::move(field),
                                  ui::boxSlot().fillWidth().preferredSize({0.0f, editor_density::kRowHeight}));
                    }
                    else if (prop.typeHint == "Vec3") {
                        glm::vec3 value = anyAs<glm::vec3>(prop.value);
                        for (int axis = 0; axis < 3; ++axis) {
                            auto drag = std::make_shared<UIDragFloat>(
                                std::format("{}_Prop{}Axis{}", key, p, axis));
                            drag->setStyleKey(editorStyle(StyleKey::DragFloat));
                            drag->setValue(value[axis], false);
                            drag->_onValueChanged = [this, i, p, axis](float component) {
                                LuaScriptComponent* target = scriptComponentOn(_layer, _entityUuid);
                                if (!target || i >= target->scripts.size() ||
                                    p >= target->scripts[i].properties.size()) {
                                    return;
                                }
                                auto& inst = target->scripts[i];
                                glm::vec3 vec = anyAs<glm::vec3>(inst.properties[p].value);
                                vec[axis] = component;
                                storeOverride(inst, inst.properties[p], vec);
                                noteMutated();
                            };
                            row.child(drag, ui::boxSlot().fillWidth().preferredSize({0.0f, editor_density::kRowHeight}));
                        }
                    }
                    else {
                        row.child(ui::text(std::format("{}_Prop{}Unsupported", key, p))
                                      .setText("[" + prop.typeHint + "]")
                                      .setStyleKey("text.muted"),
                                  ui::boxSlot().fillWidth());
                    }
                    props.child(std::move(row));
                }
            }
        }

        auto remove = labeledButton(key + "_Remove", "Remove Script");
        remove.setOnClick([this, i]() {
            LuaScriptComponent* target = scriptComponentOn(_layer, _entityUuid);
            if (!target || i >= target->scripts.size()) {
                return;
            }
            target->scripts.erase(target->scripts.begin() + static_cast<std::ptrdiff_t>(i));
            noteMutated();
            _fingerprint.clear();
        });

        auto body = ui::column(key + "_Body")
                        .setSpacing(editor_density::kRowSpacing)
                        .child(std::move(enabled))
                        .child(ui::row(key + "_PathRow")
                                   .setSpacing(editor_density::kControlSpacing)
                                   .child(std::move(pathField), ui::boxSlot().fillWidth())
                                   .child(std::move(browse),
                                          ui::boxSlot().preferredSize({72.0f, editor_density::kRowHeight})))
                        .child(ui::text(key + "_Status").setText(status).setStyleKey(statusStyle))
                        .child(std::move(props))
                        .child(std::move(remove),
                               ui::boxSlot().preferredSize({0.0f, editor_density::kToolbarHeight}));

        auto expander = ui::collapsingHeader(key)
                            .setTitle(script.scriptPath.empty() ? "[Empty Script]" : script.scriptPath)
                            .setExpanded(true)
                            .child(std::move(body))
                            .share();
        _rowWidgets.push_back(expander);
        if (tree) {
            (void)tree->attach(*_rows, expander);
        }
        else {
            _rows->addDetachedChild(expander);
        }
    }
}

void EditorLuaScriptSection::sync(WidgetTree&)
{
    const std::string next = fingerprint();
    if (next == _fingerprint && !_fingerprint.empty()) {
        return;
    }
    _fingerprint = next;
    rebuildRows();
}

} // namespace ya
