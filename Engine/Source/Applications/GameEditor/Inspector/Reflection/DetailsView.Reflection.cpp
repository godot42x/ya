#include "GameEditor/Inspector/DetailsViewInternal.h"
#include "ECS/Component/2D/BillboardComponent.h"
#include "Gameplay/Systems/Components/DirectionalLightComponent.h"
#include "ECS/Component/Material/MaterialComponent.h"
#include "ECS/Component/ModelComponent.h"
#include "ECS/Component/Mesh/StaticMeshComponent.h"
#include "ECS/Component/Mesh/SkinnedMeshComponent.h"
#include "Gameplay/Systems/Components/PointLightComponent.h"
#include "Scene3D/TransformComponent.h"
#include "Gameplay/Systems/Components/TerrainComponent.h"
#include "Gameplay/Systems/Components/UIComponent.h"
#include "ECS/Component/Material/SimpleMaterialComponent.h"
#include "ECS/Component/Material/UnlitMaterialComponent.h"
#include "ECS/Component/Material/PhongMaterialComponent.h"
#include "ECS/Component/Material/PBRMaterialComponent.h"
#include "ECS/Component/RenderComponent.h"
#include "Scene/Core/Scene.h"

namespace ya
{

bool DetailsView::isManagedLightBillboard(Entity& entity) const
{
    if (!entity.hasComponent<BillboardComponent>()) {
        return false;
    }
    auto* billboard = entity.getComponent<BillboardComponent>();
    return billboard && billboard->bManagedByLight;
}

bool DetailsView::canRemoveComponent(Entity& entity, type_index_t typeIndex) const
{
    if (typeIndex == type_index_v<BillboardComponent>) {
        return !isManagedLightBillboard(entity);
    }

    return true;
}

bool DetailsView::canAddComponent(Entity& entity, type_index_t typeIndex) const
{
    if (typeIndex == type_index_v<BillboardComponent>) {
        return !entity.hasComponents<PointLightComponent>() && !entity.hasComponents<DirectionalLightComponent>();
    }

    return true;
}

void DetailsView::drawReflectedFallbackComponents(Entity& entity)
{
    drawReflectedFallbackComponents(std::vector<Entity*>{&entity});
}

void DetailsView::drawReflectedFallbackComponents(const std::vector<Entity*>& entities)
{
    if (entities.empty()) {
        return;
    }
    static const std::unordered_set<type_index_t> kHandwrittenTypes = [] {
        return std::unordered_set<type_index_t>{
            type_index_v<TransformComponent>,
            type_index_v<ModelComponent>,
            type_index_v<StaticMeshComponent>,
            type_index_v<SkinnedMeshComponent>,
            type_index_v<TerrainComponent>,
            type_index_v<UIComponent>,
            type_index_v<DirectionalLightComponent>,
            type_index_v<PointLightComponent>,
            type_index_v<SimpleMaterialComponent>,
            type_index_v<RenderComponent>,
            type_index_v<BillboardComponent>,
            type_index_v<SkyboxComponent>,
            type_index_v<EnvironmentLightingComponent>,
            type_index_v<UnlitMaterialComponent>,
            type_index_v<PhongMaterialComponent>,
            type_index_v<PBRMaterialComponent>,
            type_index_v<LuaScriptComponent>,
        };
    }();

    const auto& typeIndexCache = ECSRegistry::get().getTypeIndexCache();
    std::vector<std::pair<std::string, type_index_t>> entries;
    entries.reserve(typeIndexCache.size());
    for (const auto& [name, ti] : typeIndexCache) {
        if (kHandwrittenTypes.contains(ti)) {
            continue;
        }
        entries.emplace_back(name.toString(), ti);
    }
    std::sort(entries.begin(), entries.end(), [](const auto& a, const auto& b) { return a.first < b.first; });

    for (const auto& [name, ti] : entries) {
        std::vector<void*> instances;
        bool              bShared = true;
        for (Entity* entity : entities) {
            if (!entity || !entity->isValid() || !entity->getScene()) {
                bShared = false;
                break;
            }
            void* component = ECSRegistry::get().getComponent(ti, entity->getScene()->getRegistry(), entity->getHandle());
            if (!component) {
                bShared = false;
                break;
            }
            instances.push_back(component);
        }
        if (!bShared || instances.empty()) {
            continue;
        }
        drawReflectedFallbackOne(name, ti, instances, entities);
    }
}

void DetailsView::drawReflectedFallbackOne(const std::string& name,
                                           type_index_t       typeIndex,
                                           std::vector<void*>& instances,
                                           const std::vector<Entity*>& entities)
{
    const std::string label = name + "  (auto)";
    const bool        bCanRemove = std::all_of(entities.begin(), entities.end(), [&](Entity* entity) {
        return canRemoveComponent(*entity, typeIndex);
    });

    componentSectionShell(
        label,
        reinterpret_cast<const void*>(static_cast<uintptr_t>(typeIndex)),
        bCanRemove,
        [&] {
            const auto* cls = ClassRegistry::instance().getClass(typeIndex);
            if (cls) {
                ya::RenderContext ctx;
                ctx.beginInstance(instances.front());
                ya::renderReflectedType(name, typeIndex, instances.front(), ctx, 0);
                if (ctx.hasModifications() && instances.size() > 1) {
                    applyModificationsToInstances(ctx.modifications, typeIndex, instances);
                }
            }
            else {
                ImGui::TextDisabled("No reflection info; fields not editable.");
            }
        },
        [&] {
            for (size_t i = 0; i < entities.size() && i < instances.size(); ++i) {
                if (Scene* scene = entities[i]->getScene()) {
                    ECSRegistry::get().removeComponent(typeIndex, scene->getRegistry(), entities[i]->getHandle());
                }
            }
        });
}

void DetailsView::drawAddComponentButton(Entity& entity)
{
    drawAddComponentButton(std::vector<Entity*>{&entity});
}

void DetailsView::drawAddComponentButton(const std::vector<Entity*>& entities)
{
    float buttonWidth = 200.0f;
    float windowWidth = ImGui::GetContentRegionAvail().x;
    float cursorPosX  = (windowWidth - buttonWidth) * 0.5f;
    if (cursorPosX > 0) {
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + cursorPosX);
    }

    if (ImGui::Button("Add Component", ImVec2(buttonWidth, 0))) {
        ImGui::OpenPopup("AddComponentPopup");
    }

    if (ImGui::BeginPopup("AddComponentPopup")) {
        auto& ecsRegistry = ECSRegistry::get();

        static char searchFilter[128] = "";
        ImGui::InputTextWithHint("##ComponentSearch", "Search...", searchFilter, sizeof(searchFilter));
        ImGui::Separator();

        std::string filterStr = searchFilter;
        std::ranges::transform(filterStr, filterStr.begin(), ::tolower);

        for (auto& [fname, typeIndex] : ecsRegistry.getTypeIndexCache()) {
            std::string componentName = fname.toString();

            if (!filterStr.empty()) {
                std::string lowerName = componentName;
                std::ranges::transform(lowerName, lowerName.begin(), ::tolower);
                if (lowerName.find(filterStr) == std::string::npos) {
                    continue;
                }
            }

            bool bAllHave    = true;
            bool bAnyAddable = false;
            for (Entity* entity : entities) {
                if (!entity || !entity->isValid() || !entity->getScene()) {
                    bAllHave = false;
                    continue;
                }
                if (ecsRegistry.hasComponent(typeIndex, entity->getScene()->getRegistry(), entity->getHandle())) {
                    continue;
                }
                bAllHave = false;
                if (canAddComponent(*entity, typeIndex)) {
                    bAnyAddable = true;
                }
            }

            if (bAllHave || !bAnyAddable) {
                ImGui::BeginDisabled();
                ImGui::MenuItem(componentName.c_str());
                ImGui::EndDisabled();
            }
            else if (ImGui::MenuItem(componentName.c_str())) {
                for (Entity* entity : entities) {
                    if (!entity || !entity->isValid() || !entity->getScene()) {
                        continue;
                    }
                    if (ecsRegistry.hasComponent(typeIndex, entity->getScene()->getRegistry(), entity->getHandle())) {
                        continue;
                    }
                    if (!canAddComponent(*entity, typeIndex)) {
                        continue;
                    }
                    if (void* compPtr = ecsRegistry.addComponent(typeIndex, entity->getScene()->getRegistry(), entity->getHandle())) {
                        YA_CORE_INFO("Added component '{}' to entity '{}' {}", componentName, entity->getName(), compPtr);
                    }
                }
                ImGui::CloseCurrentPopup();
            }
        }

        ImGui::EndPopup();
    }
}

} // namespace ya
