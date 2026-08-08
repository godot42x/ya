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
#include "Editor/EditorLayer.h"
#include "Editor/Inspector/DetailsViewInternal.h"
#include "Host/GUI/ImGui/ImGuiSystem.h"
#include "GUI/Runtime/Scene/Node2D.h"
#include "GUI/Runtime/Scene/Node.h"
#include "Scene/Core/Scene.h"
#include "Host/App.h"

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

    if (Node2D* node2D = _owner->getSelectedNode2D()) {
        drawNode2D(*node2D);
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

void DetailsView::drawNode2D(Node2D& node)
{
    ImGui::Text("Node2D");
    ImGui::SameLine();
    ImGui::TextDisabled("(%s)", node.getUITypeName());
    ImGui::Separator();

    {
        char buffer[256];
        strncpy_s(buffer, node.getName().c_str(), sizeof(buffer) - 1);
        buffer[sizeof(buffer) - 1] = '\0';
        if (ImGui::InputText("Name", buffer, sizeof(buffer))) {
            node.setName(buffer);
        }
    }

    // Reflected fields: base Node2D (_position/_size/_visibility/_zOrder) plus
    // the concrete subtype's own fields. Edits apply immediately (the UI pass
    // re-renders every frame).
    ya::RenderContext ctx;
    ctx.beginInstance(&node);
    ya::renderReflectedType(node.getUITypeName(), node.getTypeIndex(), &node, ctx, 0);
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
            if (auto* envProcessor = App::get()->getEnvironmentLightingProcessor()) {
                envProcessor->markTerrainDirty(static_cast<entt::entity>(entity),
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
            if (auto* envProcessor = App::get()->getEnvironmentLightingProcessor()) {
                if (Entity* owner = terrain->getOwner()) {
                    envProcessor->markTerrainDirty(static_cast<entt::entity>(*owner),
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
