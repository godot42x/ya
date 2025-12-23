#include "SceneHierarchyPanel.h"

#include "ECS/Component.h"
#include "ECS/Component/Material/LitMaterialComponent.h"
#include "ECS/Component/Material/SimpleMaterialComponent.h"
#include "ECS/Component/Material/UnlitMaterialComponent.h"
#include "ECS/Component/PointLightComponent.h"
#include "ECS/Component/TransformComponent.h"
#include "Scene/Scene.h"
#include <glm/gtc/type_ptr.hpp>
#include <imgui.h>


namespace ya
{

void SceneHierarchyPanel::sceneTree()
{
    ImGui::SetNextWindowSize(ImVec2(300, 600), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Scene Hierarchy"))
    {
        ImGui::End();
        return;
    }

    if (_context)
    {
        auto view = _context->getRegistry().view<TransformComponent>();
        for (auto entity : view)
        {
            Entity *ent = _context->getEntityByEnttID(entity);
            drawEntityNode(*ent);
        }
    }

    // Right-click on blank space to deselect
    if (ImGui::IsMouseDown(0) && ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows))
    {
        _selection = Entity();
    }

    ImGui::End();
}


void SceneHierarchyPanel::detailsView()
{
    // Properties panel
    ImGui::SetNextWindowSize(ImVec2(300, 600), ImGuiCond_FirstUseEver);
    if (ImGui::Begin("Properties"))
    {
        if (_selection)
        {
            drawComponents(_selection);
        }
    }
    ImGui::End();
}

void SceneHierarchyPanel::onImGuiRender()
{
    sceneTree();
    detailsView();
}

void SceneHierarchyPanel::drawEntityNode(Entity &entity)
{
    if (!entity)
    {
        return;
    }

    const char *name     = entity.getName().c_str();
    bool        selected = (entity == _selection);

    ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow;
    if (selected)
    {
        flags |= ImGuiTreeNodeFlags_Selected;
    }

    bool opened = ImGui::TreeNodeEx((void *)(intptr_t)entity.getId(), flags, "%s", name);

    if (ImGui::IsItemClicked())
    {
        setSelection(entity);
    }

    if (opened)
    {
        ImGui::TreePop();
    }
}

template <typename T, typename UIFunction>
static void drawComponent(const std::string &name, Entity entity, UIFunction uiFunc)
{
    const auto treeNodeFlags = ImGuiTreeNodeFlags_DefaultOpen |
                               ImGuiTreeNodeFlags_AllowItemOverlap |
                               ImGuiTreeNodeFlags_SpanAvailWidth |
                               ImGuiTreeNodeFlags_FramePadding |
                               ImGuiTreeNodeFlags_Framed;

    if (entity.hasComponent<T>())
    {
        auto  *component                = entity.getComponent<T>();
        ImVec2 content_region_available = ImGui::GetContentRegionAvail();

        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2{4, 4});
        float line_height = ImGui::GetFont()->LegacySize + ImGui::GetStyle().FramePadding.y * 2.f;
        ImGui::Separator();

        bool bOpen = ImGui::TreeNodeEx((void *)typeid(T).hash_code(), treeNodeFlags, "%s", name.c_str());

        ImGui::PopStyleVar();
        ImGui::SameLine(content_region_available.x - line_height * 0.5f);

        if (ImGui::Button("+", ImVec2{line_height, line_height})) {
            ImGui::OpenPopup("ComponentSettings");
        }

        bool bRemoveComponent = false;

        if (ImGui::BeginPopup("ComponentSettings"))
        {
            if (ImGui::MenuItem("Remove Component")) {
                bRemoveComponent = true;
            }
            ImGui::EndPopup();
        }

        if (bOpen)
        {
            uiFunc(component);
            ImGui::TreePop();
        }

        if (bRemoveComponent)
        {
            entity.removeComponent<T>();
        }
    }
}


void SceneHierarchyPanel::drawComponents(Entity &entity)
{
    if (!entity)
    {
        return;
    }

    // Draw entity name at top
    ImGui::Text("Entity ID: %u", entity.getId());
    ImGui::Separator();

    drawComponent<TransformComponent>("Transform", entity, [](TransformComponent *tc) {
        bool bDirty = false;
        bDirty |= ImGui::DragFloat3("Position", glm::value_ptr(tc->_position), 0.1f);
        bDirty |= ImGui::DragFloat3("Rotation", glm::value_ptr(tc->_rotation), 0.1f);
        bDirty |= ImGui::DragFloat3("Scale", glm::value_ptr(tc->_scale), 0.1f);
        tc->bDirty = bDirty;
    });

    drawComponent<SimpleMaterialComponent>("Simple Material", entity, [](SimpleMaterialComponent *smc) {
        for (auto [material, meshId] : smc->getMaterial2MeshIDs()) {
            auto simpleMat = material->as<SimpleMaterial>();
            bool bDirty    = false;
            int  colorType = static_cast<int>(simpleMat->colorType);
            if (ImGui::Combo("Color Type", &colorType, "Normal\0Texcoord\0\0")) {
                simpleMat->colorType = static_cast<SimpleMaterial::EColor>(colorType);
            }
        }
    });

    drawComponent<UnlitMaterialComponent>("Unlit Material", entity, [](UnlitMaterialComponent *umc) {
        for (auto [mat, meshIndex] : umc->getMaterial2MeshIDs()) {
            ImGui::CollapsingHeader(mat->getLabel().c_str(), 0);
            ImGui::Indent();

            auto unlitMat = mat->as<UnlitMaterial>();
            bool bDirty   = false;
            bDirty |= ImGui::DragFloat3("Base Color0", glm::value_ptr(unlitMat->uMaterial.baseColor0), 0.1f);
            bDirty |= ImGui::DragFloat3("Base Color1", glm::value_ptr(unlitMat->uMaterial.baseColor1), 0.1f);
            bDirty |= ImGui::DragFloat("Mix Value", &unlitMat->uMaterial.mixValue, 0.01f, 0.0f, 1.0f);
            for (uint32_t i = 0; i < unlitMat->_textureViews.size(); i++) {
                // if (ImGui::CollapsingHeader(std::format("Texture{}", i).c_str(), ImGuiTreeNodeFlags_DefaultOpen)) {
                auto &tv    = unlitMat->_textureViews[i];
                auto  label = tv.texture->getLabel();
                if (label.empty()) {
                    label = tv.texture->getFilepath();
                }
                ImGui::Text("Texture %d: %s", i, label.c_str());
                bDirty |= ImGui::Checkbox(std::format("Enable##{}", i).c_str(), &tv.bEnable);
                bDirty |= ImGui::DragFloat2(std::format("Offset##{}", i).c_str(), glm::value_ptr(tv.uvTranslation), 0.01f);
                bDirty |= ImGui::DragFloat2(std::format("Scale##{}", i).c_str(), glm::value_ptr(tv.uvScale), 0.01f, 0.01f, 10.0f);
                static constexpr const auto pi = glm::pi<float>();
                bDirty |= ImGui::DragFloat(std::format("Rotation##{}", i).c_str(), &tv.uvRotation, pi / 3600, -pi, pi);
            }
            if (bDirty) {
                unlitMat->setParamDirty(true);
            }
            ImGui::Unindent();
        }
    });

    drawComponent<LitMaterialComponent>("Lit Material", entity, [](LitMaterialComponent *lmc) {
        for (auto [mat, meshIndex] : lmc->getMaterial2MeshIDs()) {
            ImGui::CollapsingHeader(mat->getLabel().c_str(), 0);
            ImGui::Indent();

            auto litMat = mat->as<LitMaterial>();
            bool bDirty = false;
            bDirty |= ImGui::DragFloat3("Object Color", glm::value_ptr(litMat->uParams.objectColor), 0.1f);
            // bDirty |= ImGui::DragFloat3("Base Color", glm::value_ptr(litMat->uMaterial.baseColor), 0.1f);
            // bDirty |= ImGui::DragFloat("Metallic", &litMat->uMaterial.metallic, 0.01f, 0.0f, 1.0f);
            // bDirty |= ImGui::DragFloat("Roughness", &litMat->uMaterial.roughness, 0.01f, 0.0f, 1.0f);
            // for (uint32_t i = 0; i < litMat->_textureViews.size(); i++) {
            //     auto &tv    = litMat->_textureViews[i];
            //     auto  label = tv.texture->getLabel();
            //     if (label.empty()) {
            //         label = tv.texture->getFilepath();
            //     }
            //     ImGui::Text("Texture %d: %s", i, label.c_str());
            //     bDirty |= ImGui::Checkbox(std::format("Enable##{}", i).c_str(), &tv.bEnable);
            //     bDirty |= ImGui::DragFloat2(std::format("Offset##{}", i).c_str(), glm::value_ptr(tv.uvTranslation), 0.01f);
            //     bDirty |= ImGui::DragFloat2(std::format("Scale##{}", i).c_str(), glm::value_ptr(tv.uvScale), 0.01f, 0.01f, 10.0f);
            //     static constexpr const auto pi = glm::pi<float>();
            //     bDirty |= ImGui::DragFloat(std::format("Rotation##{}", i).c_str(), &tv.uvRotation, pi / 3600, -pi, pi);
            // }
            if (bDirty) {
                litMat->setParamDirty(true);
            }
            ImGui::Unindent();
        }
    });



    drawComponent<PointLightComponent>("Point Light", entity, [](PointLightComponent *plc) {
        bool bDirty = false;
        bDirty |= ImGui::ColorEdit3("Color", glm::value_ptr(plc->_color));
        bDirty |= ImGui::DragFloat("Intensity", &plc->_intensity, 0.1f, 0.0f, 100.0f);
        bDirty |= ImGui::DragFloat("Radius", &plc->_range, 0.1f, 0.0f, 100.0f);
    });
}

void SceneHierarchyPanel::setSelection(Entity newSelection)
{
    if (_selection != newSelection)
    {
        _lastSelection = _selection;
        _selection     = newSelection;
    }
}

} // namespace ya
