#include "Editor/Inspector/DetailsViewInternal.h"

namespace ya
{

void DetailsView::drawReflectedFallbackComponents(Entity& entity)
{
    Scene* scene = entity.getScene();
    if (!scene) {
        return;
    }
    auto& registry = scene->getRegistry();

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
        void* component = ECSRegistry::get().getComponent(ti, registry, entity.getHandle());
        if (!component) {
            continue;
        }
        drawReflectedFallbackOne(name, ti, component, entity);
    }
}

void DetailsView::drawReflectedFallbackOne(const std::string& name,
                                           type_index_t       typeIndex,
                                           void*              component,
                                           Entity&            entity)
{
    const std::string label = name + "  (auto)";

    componentSectionShell(
        label,
        reinterpret_cast<const void*>(static_cast<uintptr_t>(typeIndex)),
        [&] {
            const auto* cls = ClassRegistry::instance().getClass(typeIndex);
            if (cls) {
                ya::RenderContext ctx;
                ctx.beginInstance(component);
                ya::renderReflectedType(name, typeIndex, component, ctx, 0);
            }
            else {
                ImGui::TextDisabled("No reflection info; fields not editable.");
            }
        },
        [&] {
            if (Scene* scene = entity.getScene()) {
                ECSRegistry::get().removeComponent(typeIndex, *scene, entity.getHandle());
            }
        });
}

void DetailsView::drawAddComponentButton(Entity& entity)
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

            auto* scene = entity.getScene();
            if (ecsRegistry.hasComponent(typeIndex, *scene, entity.getHandle())) {
                ImGui::BeginDisabled();
                ImGui::MenuItem(componentName.c_str());
                ImGui::EndDisabled();
            }
            else {
                if (ImGui::MenuItem(componentName.c_str())) {
                    if (void* compPtr = ecsRegistry.addComponent(typeIndex, *scene, entity.getHandle())) {
                        YA_CORE_INFO("Added component '{}' to entity '{}' {}", componentName, entity.getName(), compPtr);
                    }
                    ImGui::CloseCurrentPopup();
                }
            }
        }

        ImGui::EndPopup();
    }
}

} // namespace ya
