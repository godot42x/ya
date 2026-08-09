#include "Core/Profiling/Instrumentor.h"
#include "Core/Reflection/ReflectionSerializer.h"
#include "ECS/Component/2D/BillboardComponent.h"
#include "Gameplay/Systems/Components/UIComponent.h"
#include "Gameplay/Systems/Components/DirectionalLightComponent.h"
#include "Gameplay/Systems/Components/LuaScriptComponent.h"
#include "ECS/Component/Material/SimpleMaterialComponent.h"
#include "ECS/Component/Material/UnlitMaterialComponent.h"
#include "Render3D/Material/SimpleMaterial.h"
#include "ECS/Component/Mesh/SkinnedMeshComponent.h"
#include "ECS/Component/Mesh/StaticMeshComponent.h"
#include "ECS/Component/ModelComponent.h"
#include "Gameplay/Systems/Components/PointLightComponent.h"
#include "ECS/Component/RenderComponent.h"
#include "Gameplay/Systems/Components/TerrainComponent.h"
#include "Scene3D/TransformComponent.h"
#include "Render3D/EnvironmentLighting/EnvironmentLightingProcessor.h"
#include "Render3D/Terrain/TerrainProcessor.h"
#include "Editor/EditorLayer.h"
#include "Editor/Inspector/DetailsViewInternal.h"
#include "Host/GUI/ImGui/ImGuiSystem.h"
#include "Hierarchy/Node.h"
#include "Scene/Core/Scene.h"
#include "Host/App.h"
#include "GUI/Widgets/UITypeRegistry.h"

namespace ya
{

namespace
{

/// Walk a dotted reflection path (e.g. "_params.albedo") from a root instance,
/// resolving own + base-class properties, and return the instance that OWNS
/// the leaf property together with the leaf property. Base-class fields render
/// without a "__base__" path prefix, so parents are searched transparently.
bool resolveReflectionPath(void*               rootInstance,
                           type_index_t        rootTypeIndex,
                           const std::string&  propPath,
                           void*&              outOwningInstance,
                           const Property*&    outProperty)
{
    auto&       registry = ClassRegistry::instance();
    const Class* cls     = registry.getClass(rootTypeIndex);
    void*       owner    = rootInstance;

    std::string remaining = propPath;
    while (cls && owner && !remaining.empty()) {
        const auto    dot     = remaining.find('.');
        const std::string segment = remaining.substr(0, dot);
        const bool    bLast   = (dot == std::string::npos);
        remaining             = bLast ? std::string{} : remaining.substr(dot + 1);

        // Find the property in this class or any base class, adjusting the
        // owning pointer as we descend into parents.
        const Property* prop         = nullptr;
        void*           segmentOwner = owner;
        {
            const Class* searchCls  = cls;
            void*        searchOwner = owner;
            while (searchCls) {
                if (const Property* p = searchCls->getProperty(segment)) {
                    prop         = p;
                    segmentOwner = searchOwner;
                    break;
                }
                bool bFoundParent = false;
                for (auto parentTypeId : searchCls->parents) {
                    if (Class* parentCls = registry.getClass(parentTypeId)) {
                        if (void* parentPtr = searchCls->getParentPointer(searchOwner, parentTypeId)) {
                            searchCls   = parentCls;
                            searchOwner = parentPtr;
                            bFoundParent = true;
                            break;
                        }
                    }
                }
                if (!bFoundParent) {
                    searchCls = nullptr;
                }
            }
            if (!prop) {
                return false;
            }
        }

        if (bLast) {
            outOwningInstance = segmentOwner;
            outProperty       = prop;
            return true;
        }

        // Descend into the property value for the next segment.
        owner = prop->getMutableAddress(segmentOwner);
        if (prop->bPointer) {
            owner = owner ? *static_cast<void**>(owner) : nullptr;
            cls   = registry.getClass(prop->pointeeTypeIndex);
        }
        else {
            cls = registry.getClass(prop->typeIndex);
        }
    }
    return false;
}

} // namespace

void DetailsView::onImGuiRender()
{
    YA_PROFILE_FUNCTION();
    ImGui::SetNextWindowSize(ImVec2(300, 600), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Properties")) {
        ImGui::End();
        return;
    }

    if (SceneWidgetEntry* entry = _owner->getSelectedWidgetEntry()) {
        if (Scene* scene = _owner->getViewportInteractionScene()) {
            drawWidgetEntry(*scene, *entry);
        }
    }
    else if (const auto& selections = _owner->getSelections(); selections.size() > 1) {
        drawMultiComponents(selections);
    }
    else if (!selections.empty()) {
        if (auto* firstEntity = selections[0]; firstEntity->isValid()) {
            drawComponents(*firstEntity);
        }
    }

    ImGui::End();
    _filePicker.render();
}

void DetailsView::drawWidgetEntry(Scene& scene, SceneWidgetEntry& entry)
{
    ImGui::Text("Game UI Entry");
    ImGui::SameLine();
    ImGui::TextDisabled("(%s)", entry.entryId.c_str());
    ImGui::Separator();

    if (entry.inlineDocument) {
        ImGui::Text("Type");
        ImGui::SameLine();
        ImGui::TextUnformatted(entry.inlineDocument->typeId.c_str());
        ImGui::SameLine();
        if (ImGui::SmallButton("Open in UI Designer")) {
            _owner->getUIDesignerPanel().openSceneEntry(scene, entry);
        }
    }
    else if (!entry.documentPath.empty()) {
        ImGui::Text("Document");
        ImGui::SameLine();
        ImGui::TextUnformatted(entry.documentPath.c_str());
        App* app = App::get();
        const bool bResolved = app && app->getGameUIHost() &&
                               app->getGameUIHost()->getDocumentResolver().isResolved(entry.documentPath);
        ImGui::SameLine();
        ImGui::TextDisabled(bResolved ? "(resolved)" : "(not resolved)");
        if (ImGui::SmallButton("Open in UI Designer")) {
            _owner->getUIDesignerPanel().openDocumentPath(entry.documentPath);
        }
        ImGui::SameLine();
        if (ImGui::SmallButton("Reload")) {
            if (app && app->getGameUIHost()) {
                app->getGameUIHost()->getDocumentResolver().invalidate(entry.documentPath);
                app->getGameUIHost()->reloadMountedSceneUI();
            }
        }
    }
    else {
        ImGui::TextDisabled("<invalid: no document>");
    }

    ImGui::DragInt("zOrder", &entry.zOrder, 1, -1000, 1000);
    ImGui::Checkbox("autoMount", &entry.autoMount);

    std::string documentPath = entry.documentPath;
    if (drawPathInput("Document Path", documentPath, DETAILS_SCRIPT_INPUT_BUFFER_SIZE)) {
        entry.documentPath = documentPath;
    }

    drawEntryOverrides(entry);

    if (ImGui::Button("Delete Entry")) {
        scene.removeWidgetEntry(entry.entryId);
        _owner->setSelectedWidgetEntryId("");
    }
}

void DetailsView::drawEntryOverrides(SceneWidgetEntry& entry)
{
    ImGui::SeparatorText("Instance Overrides");
    ImGui::TextDisabled("Only InstanceEditable fields may be overridden.");

    // Collect the instance-editable field names of the entry's root type
    // (cached per typeId for the editor session).
    static std::unordered_map<std::string, std::vector<std::string>> editableFieldsCache;
    std::shared_ptr<UIDocument> document = entry.inlineDocument;
    if (!document && !entry.documentPath.empty()) {
        if (App* app = App::get(); app && app->getGameUIHost()) {
            document = app->getGameUIHost()->getDocumentResolver().load(entry.documentPath);
        }
    }
    const std::string typeId = document ? document->typeId : std::string();
    const std::vector<std::string>* editable = nullptr;
    if (!typeId.empty()) {
        if (auto it = editableFieldsCache.find(typeId); it != editableFieldsCache.end()) {
            editable = &it->second;
        }
        else {
            auto& fields = editableFieldsCache[typeId];
            auto* cls = ClassRegistry::instance().getClass(typeId);
            if (!cls && document) {
                // Fall back through a transient instance when the registry
                // type name differs from the C++ class name.
                if (auto widget = UITypeRegistry::instance().createInstance(typeId)) {
                    cls = ClassRegistry::instance().getClass(widget->getTypeIndex());
                }
            }
            if (cls) {
                std::vector<const Class*> chain{cls};
                for (size_t i = 0; i < chain.size(); ++i) {
                    const Class* current = chain[i];
                    for (const auto& [name, prop] : current->properties) {
                        (void)name;
                        if (prop.metadata.hasFlag(FieldFlags::InstanceEditable)) {
                            fields.push_back(prop.name);
                        }
                    }
                    for (const auto parentTypeId : current->parents) {
                        if (const Class* parent = current->getClassByTypeId(parentTypeId)) {
                            chain.push_back(parent);
                        }
                    }
                }
                std::sort(fields.begin(), fields.end());
            }
            editable = &fields;
        }
    }

    // Existing overrides with delete.
    for (auto it = entry.overrides.fieldOverrides.begin(); it != entry.overrides.fieldOverrides.end();) {
        ImGui::PushID(it->first.c_str());
        ImGui::TextUnformatted(it->first.c_str());
        ImGui::SameLine();
        ImGui::TextDisabled("%s", it->second.dump().c_str());
        ImGui::SameLine();
        if (ImGui::SmallButton("X")) {
            it = entry.overrides.fieldOverrides.erase(it);
            ImGui::PopID();
            continue;
        }
        ImGui::PopID();
        ++it;
    }

    // Add: pick an instance-editable field not yet overridden + raw JSON value.
    static char sOverrideField[128] = "";
    static char sOverrideValue[256] = "";
    ImGui::SetNextItemWidth(180.0f);
    if (ImGui::BeginCombo("Field", sOverrideField[0] ? sOverrideField : "(select)")) {
        if (editable) {
            for (const std::string& name : *editable) {
                if (entry.overrides.fieldOverrides.contains(name)) {
                    continue;
                }
                const bool bSelected = sOverrideField == name;
                if (ImGui::Selectable(name.c_str(), bSelected)) {
                    std::strncpy(sOverrideField, name.c_str(), sizeof(sOverrideField) - 1);
                }
            }
        }
        ImGui::EndCombo();
    }
    ImGui::SameLine();
    ImGui::SetNextItemWidth(220.0f);
    ImGui::InputText("Value", sOverrideValue, sizeof(sOverrideValue));
    ImGui::SameLine();
    if (ImGui::SmallButton("Add") && sOverrideField[0] != '\0') {
        nlohmann::json value;
        try {
            value = nlohmann::json::parse(sOverrideValue);
        }
        catch (...) {
            value = sOverrideValue; // store as a plain string
        }
        entry.overrides.fieldOverrides[sOverrideField] = std::move(value);
        sOverrideField[0]  = '\0';
        sOverrideValue[0]  = '\0';
    }
}

void DetailsView::applyModificationsToInstances(const std::vector<RenderModificationRecord>& modifications,
                                                type_index_t                               rootTypeIndex,
                                                const std::vector<void*>&                  instances)
{
    if (instances.size() < 2) {
        return;
    }

    for (const auto& mod : modifications) {
        void*           leafOwner = nullptr;
        const Property* leafProp  = nullptr;
        if (!resolveReflectionPath(instances.front(), rootTypeIndex, mod.propPath, leafOwner, leafProp)) {
            YA_CORE_WARN("Multi-edit: cannot resolve property path '{}'; skipped", mod.propPath);
            continue;
        }

        const nlohmann::json value = ReflectionSerializer::serializeProperty(leafOwner, *leafProp);
        for (size_t i = 1; i < instances.size(); ++i) {
            void*           otherOwner = nullptr;
            const Property* otherProp  = nullptr;
            if (resolveReflectionPath(instances[i], rootTypeIndex, mod.propPath, otherOwner, otherProp)) {
                ReflectionSerializer::deserializeProperty(*otherProp, otherOwner, value);
            }
        }
    }
}

void DetailsView::drawComponents(Entity& entity)
{
    YA_PROFILE_FUNCTION();
    if (!entity) {
        return;
    }

    ImGui::Text("Entity ID: %u", entity.getId());
    ImGui::SameLine();
    drawAddComponentButton(entity);
    ImGui::Separator();

    Scene* scene = entity.getScene();
    Node*  node  = scene ? scene->getNodeByEntity(&entity) : nullptr;

    {
        ImGuiStyleScope style;
        style.pushColor(ImGuiCol_Text, ImVec4(0.9f, 0.9f, 0.5f, 1.0f));
        ImGui::PushID("Name");
        char        buffer[256];
        std::string name = node ? node->getName() : entity.getName();
        strncpy_s(buffer, name.c_str(), sizeof(buffer) - 1);
        buffer[sizeof(buffer) - 1] = '\0';

        if (ImGui::InputText("Name", buffer, sizeof(buffer))) {
            entity.setName(buffer);
        }
        ImGui::PopID();
    }

    drawReflectedComponent<TransformComponent>("Transform", entity, [](TransformComponent* tc) {
        tc->markLocalDirty();
        tc->propagateWorldDirtyToChildren();
    });
    drawReflectedComponent<ModelComponent>("Model", entity, [](ModelComponent* mc) {
        mc->invalidate();
    });
    drawReflectedComponent<StaticMeshComponent>("Static Mesh", entity, [](StaticMeshComponent* mc) {
        mc->invalidate();
    });
    drawReflectedComponent<SkinnedMeshComponent>("Skinned Mesh", entity, [](SkinnedMeshComponent* mc) {
        mc->invalidate();
    });
    drawReflectedComponent<TerrainComponent>("Terrain", entity, [&entity](TerrainComponent* terrain, const ya::RenderContext& ctx) {
        if (ctx.hasModifications()) {
            terrain->invalidate(App::currentFrameIndex() + 8);
            if (auto* terrainProcessor = App::get()->getTerrainProcessor()) {
                terrainProcessor->markTerrainDirty(static_cast<entt::entity>(entity),
                                                   "editor terrain modified",
                                                   App::currentFrameIndex() + 8);
            }
        }
    });
    drawReflectedComponent<UIComponent>("UI Component", entity, [](UIComponent* uc) {});

    drawReflectedComponent<DirectionalLightComponent>("Directional Light", entity, [](DirectionalLightComponent* dlc) {});
    drawReflectedComponent<PointLightComponent>("Point Light", entity, [](PointLightComponent* plc) {
    });

    drawComponent<SimpleMaterialComponent>("Simple Material", entity, [](SimpleMaterialComponent* smc) {
        auto* simpleMat = smc->getMaterial();
        if (!simpleMat) {
            ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.0f, 1.0f), "Material not resolved");
            return;
        }
        int colorType = static_cast<int>(simpleMat->colorType);
        if (ImGui::Combo("Color Type", &colorType, "Normal\0Texcoord\0\0")) {
            simpleMat->colorType = static_cast<SimpleMaterial::EColor>(colorType);
        }
    });

    drawReflectedComponent<RenderComponent>("Render Component", entity, [](RenderComponent* rc) {
    });
    drawReflectedComponent<BillboardComponent>("Billboard", entity, [](BillboardComponent* bc) {
        bc->invalidate();
    });
    drawSkyboxComponent(entity);
    drawEnvironmentLightingComponent(entity);

    drawMaterialComponent<UnlitMaterialComponent>("Unlit Material", {&entity});
    drawMaterialComponent<PhongMaterialComponent>("Phong Material", {&entity});
    drawMaterialComponent<PBRMaterialComponent>("PBR Material", {&entity}, "Invalidate##PBR");

    drawComponent<LuaScriptComponent>("Lua Script", entity, [this](LuaScriptComponent* lsc) {
        if (ImGui::Button("+ Add Script")) {
            _filePicker.openScriptPicker("", [lsc](const std::string& scriptPath) {
                lsc->addScript(scriptPath);
            });
        }

        ImGui::Separator();

        size_t indexToRemove = std::numeric_limits<size_t>::max();
        for (size_t i = 0; i < lsc->scripts.size(); ++i) {
            auto& script = lsc->scripts[i];

            ImGui::PushID(static_cast<int>(i));

            bool headerOpen = ImGui::CollapsingHeader(
                script.scriptPath.empty() ? "[Empty Script]" : script.scriptPath.c_str(),
                ImGuiTreeNodeFlags_DefaultOpen);

            ImGui::Checkbox("Enabled##enabled", &script.enabled);

            if (headerOpen) {
                ImGui::Indent();

                char buffer[DETAILS_SCRIPT_INPUT_BUFFER_SIZE];
                strncpy_s(buffer, script.scriptPath.c_str(), _TRUNCATE);

                ImGui::SetNextItemWidth(-80);
                if (ImGui::InputText("##ScriptPath", buffer, sizeof(buffer))) {
                    script.scriptPath                 = LuaScriptComponent::ScriptInstance::normalizeScriptPath(buffer);
                    script.bLoaded                    = false;
                    script.bAuthoringPreviewAttempted = false;
                }

                ImGui::SameLine();
                if (ImGui::Button("Browse...")) {
                    _filePicker.openScriptPicker(script.scriptPath, [&script](const std::string& newPath) {
                        script.scriptPath                 = LuaScriptComponent::ScriptInstance::normalizeScriptPath(newPath);
                        script.bLoaded                    = false;
                        script.bAuthoringPreviewAttempted = false;
                    });
                }

                bool hasValidPath  = !script.scriptPath.empty();
                bool hasProperties = script.self.valid() && !script.properties.empty();

                if (script.bLoaded) {
                    ImGui::TextColored(ImVec4(0, 1, 0, 1), "Status: Loaded (Runtime)");
                }
                else if (hasProperties) {
                    ImGui::TextColored(ImVec4(0.3f, 0.7f, 1.0f, 1), "Status: Preview Mode (Editor)");
                }
                else {
                    ImGui::TextColored(ImVec4(1, 0.5f, 0, 1), "Status: Not Loaded");
                }

                if (hasValidPath && !script.bLoaded && !script.bAuthoringPreviewAttempted) {
                    tryLoadScriptForEditor(&script);
                }

                if (script.self.valid()) {
                    ImGui::Separator();

                    if (script.properties.empty()) {
                        script.refreshProperties();
                    }

                    if (!script.properties.empty()) {
                        ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "Script Properties:");

                        for (auto& prop : script.properties) {
                            renderScriptProperty(&prop, &script);
                        }

                        if (ImGui::Button("Refresh Properties")) {
                            script.refreshProperties();
                        }
                    }
                    else {
                        ImGui::TextDisabled("No properties found");
                        ImGui::TextDisabled("Tip: Use _PROPERTIES table to define editable properties");
                    }
                }
                else if (hasValidPath) {
                    ImGui::Separator();
                    ImGui::TextColored(ImVec4(1, 0, 0, 1), "Failed to load script");
                    ImGui::TextDisabled("Check console for error details");
                    if (ImGui::Button("Retry Load")) {
                        script.bAuthoringPreviewAttempted = false;
                        tryLoadScriptForEditor(&script);
                    }
                }

                ImGui::Separator();

                if (ImGui::Button("Remove Script")) {
                    indexToRemove = i;
                }

                ImGui::Unindent();
            }

            ImGui::PopID();
            ImGui::Separator();
        }

        if (indexToRemove != std::numeric_limits<size_t>::max()) {
            auto eraseIt = lsc->scripts.begin() + static_cast<std::ptrdiff_t>(indexToRemove);
            lsc->scripts.erase(eraseIt);
        }
    });

    drawReflectedFallbackComponents(entity);
}

void DetailsView::drawMultiComponents(const std::vector<Entity*>& entities)
{
    YA_PROFILE_FUNCTION();
    if (entities.empty()) {
        return;
    }

    ImGui::Text("Selected: %zu entities", entities.size());
    ImGui::SameLine();
    drawAddComponentButton(entities);
    ImGui::Separator();

    // Name is editable only when every selected entity shares the same name;
    // otherwise show a disabled "(multiple values)" input.
    {
        const std::string firstName = entities.front()->getName();
        const bool        bSameName = std::all_of(entities.begin() + 1, entities.end(), [&](Entity* entity) {
            return entity->getName() == firstName;
        });

        ImGuiStyleScope style;
        style.pushColor(ImGuiCol_Text, ImVec4(0.9f, 0.9f, 0.5f, 1.0f));
        ImGui::PushID("Name");
        char buffer[256];
        if (bSameName) {
            strncpy_s(buffer, firstName.c_str(), sizeof(buffer) - 1);
            buffer[sizeof(buffer) - 1] = '\0';
        }
        else {
            strncpy_s(buffer, "", 1);
            ImGui::BeginDisabled();
        }

        if (ImGui::InputText("Name", buffer, sizeof(buffer))) {
            for (Entity* entity : entities) {
                entity->setName(buffer);
            }
        }
        if (!bSameName) {
            ImGui::EndDisabled();
        }
        ImGui::PopID();
    }

    // Components shared by every selection (intersection). Edits to a shared
    // component are written back to every instance by drawReflectedComponents.
    drawReflectedComponents<TransformComponent>("Transform", entities, [](std::vector<TransformComponent*>& tcs, const ya::RenderContext&) {
        for (TransformComponent* tc : tcs) {
            tc->markLocalDirty();
            tc->propagateWorldDirtyToChildren();
        }
    });
    drawReflectedComponents<ModelComponent>("Model", entities, [](std::vector<ModelComponent*>& mcs, const ya::RenderContext& ctx) {
        if (ctx.hasModifications()) {
            for (ModelComponent* mc : mcs) {
                mc->invalidate();
            }
        }
    });
    drawReflectedComponents<StaticMeshComponent>("Static Mesh", entities, [](std::vector<StaticMeshComponent*>& mcs, const ya::RenderContext& ctx) {
        if (ctx.hasModifications()) {
            for (StaticMeshComponent* mc : mcs) {
                mc->invalidate();
            }
        }
    });
    drawReflectedComponents<SkinnedMeshComponent>("Skinned Mesh", entities, [](std::vector<SkinnedMeshComponent*>& mcs, const ya::RenderContext& ctx) {
        if (ctx.hasModifications()) {
            for (SkinnedMeshComponent* mc : mcs) {
                mc->invalidate();
            }
        }
    });
    drawReflectedComponents<TerrainComponent>("Terrain", entities, [](std::vector<TerrainComponent*>& terrains, const ya::RenderContext& ctx) {
        if (!ctx.hasModifications()) {
            return;
        }
        for (TerrainComponent* terrain : terrains) {
            terrain->invalidate(App::currentFrameIndex() + 8);
            if (auto* terrainProcessor = App::get()->getTerrainProcessor()) {
                if (Entity* owner = terrain->getOwner()) {
                    terrainProcessor->markTerrainDirty(static_cast<entt::entity>(*owner),
                                                       "editor terrain modified",
                                                       App::currentFrameIndex() + 8);
                }
            }
        }
    });
    drawReflectedComponents<UIComponent>("UI Component", entities, [](std::vector<UIComponent*>&, const ya::RenderContext&) {});
    drawReflectedComponents<DirectionalLightComponent>("Directional Light", entities, [](std::vector<DirectionalLightComponent*>&, const ya::RenderContext&) {});
    drawReflectedComponents<PointLightComponent>("Point Light", entities, [](std::vector<PointLightComponent*>&, const ya::RenderContext&) {});
    drawReflectedComponents<RenderComponent>("Render Component", entities, [](std::vector<RenderComponent*>&, const ya::RenderContext&) {});
    drawReflectedComponents<BillboardComponent>("Billboard", entities, [](std::vector<BillboardComponent*>& bcs, const ya::RenderContext& ctx) {
        if (ctx.hasModifications()) {
            for (BillboardComponent* bc : bcs) {
                bc->invalidate();
            }
        }
    });

    drawMaterialComponent<UnlitMaterialComponent>("Unlit Material", entities);
    drawMaterialComponent<PhongMaterialComponent>("Phong Material", entities);
    drawMaterialComponent<PBRMaterialComponent>("PBR Material", entities, "Invalidate##PBR");

    drawReflectedFallbackComponents(entities);
}

} // namespace ya
