#include "Core/Profiling/Instrumentor.h"
#include "ECS/Component/2D/BillboardComponent.h"
#include "ECS/Component/2D/UIComponent.h"
#include "ECS/Component/DirectionalLightComponent.h"
#include "ECS/Component/LuaScriptComponent.h"
#include "ECS/Component/Material/SimpleMaterialComponent.h"
#include "ECS/Component/Material/UnlitMaterialComponent.h"
#include "ECS/Component/Mesh/SkinnedMeshComponent.h"
#include "ECS/Component/Mesh/StaticMeshComponent.h"
#include "ECS/Component/ModelComponent.h"
#include "ECS/Component/PointLightComponent.h"
#include "ECS/Component/RenderComponent.h"
#include "ECS/Component/Terrain/TerrainComponent.h"
#include "ECS/Component/TransformComponent.h"
#include "ECS/System/ResourceResolveSystem.h"
#include "Editor/EditorLayer.h"
#include "Editor/Inspector/DetailsViewInternal.h"
#include "Runtime/GUI/ImGui/ImGuiSystem.h"
#include "Scene/Node.h"
#include "Scene/Scene.h"
#include "Runtime/Application/App.h"

namespace ya
{

void DetailsView::onImGuiRender()
{
    YA_PROFILE_FUNCTION();
    ImGui::SetNextWindowSize(ImVec2(300, 600), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Properties")) {
        ImGui::End();
        return;
    }

    if (!_owner->getSelections().empty()) {
        if (auto* firstEntity = _owner->getSelections()[0]; firstEntity->isValid()) {
            drawComponents(*firstEntity);
        }
    }

    ImGui::End();
    _filePicker.render();
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
            if (auto* resolver = App::get()->getResourceResolveSystem()) {
                resolver->markTerrainDirty(static_cast<entt::entity>(entity),
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

    drawMaterialComponent<UnlitMaterialComponent>("Unlit Material", entity);
    drawMaterialComponent<PhongMaterialComponent>("Phong Material", entity);
    drawMaterialComponent<PBRMaterialComponent>("PBR Material", entity, "Invalidate##PBR");

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

} // namespace ya
