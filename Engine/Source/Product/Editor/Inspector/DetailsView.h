#pragma once

#include "Foundation/Core/TypeIndex.h"
#include "Framework/Game/Gameplay/ECS/Entity.h"
#include "Product/Editor/FilePicker.h"
#include "Product/Editor/Inspector/TypeRenderer.h"
#include "Framework/Game/Gameplay/ECS/Component/3D/SkyboxComponent.h"
#include "Framework/Game/Gameplay/ECS/Component/3D/EnvironmentLightingComponent.h"
#include "Framework/Game/Gameplay/ECS/Component/Material/PBRMaterialComponent.h"
#include "Framework/Game/Gameplay/ECS/Component/Material/PhongMaterialComponent.h"
#include "Framework/Game/Gameplay/ECS/Component/Material/UnlitMaterialComponent.h"
#include "Framework/Game/Gameplay/ECS/Component/Material/SimpleMaterialComponent.h"
#include <imgui.h>
#include <sol/sol.hpp>
#include <type_traits>

namespace ya
{

struct Scene;
struct EditorLayer;
struct LuaScriptComponent;
struct SkyboxPreviewInfo;
struct Texture;
struct Node2D;

// ============================================================================
// MARK: Details View
// ============================================================================

struct DetailsView
{
  private:
    EditorLayer *_owner;

    // 编辑器专用 Lua 状态（用于预览属性）
    sol::state _editorLua;
    bool       _editorLuaInitialized = false;

    // 文件选择器
    FilePicker _filePicker;

    // 递归深度追踪（防止无限递归）
    int _recursionDepth = 0;

    [[nodiscard]] bool isManagedLightBillboard(Entity& entity) const;
    [[nodiscard]] bool canRemoveComponent(Entity& entity, type_index_t typeIndex) const;
    [[nodiscard]] bool canAddComponent(Entity& entity, type_index_t typeIndex) const;

  public:
    DetailsView(EditorLayer *owner);

    void onImGuiRender();

  private:
    void drawComponents(Entity &entity);
    /// Node-level inspector: reflected fields of an entity-less Node2D node.
    void drawNode2D(Node2D &node);
    /// Multi-select mode: draw the intersection of components shared by all
    /// selected entities; edits write back to every instance.
    void drawMultiComponents(const std::vector<Entity*> &entities);
    void drawAddComponentButton(Entity &entity); // Add component popup
    void drawAddComponentButton(const std::vector<Entity*> &entities);
    void drawEnvironmentLightingComponent(Entity& entity);
    void drawEnvironmentLightingStatus(EEnvironmentLightingSourceResolveState sourceState, EEnvironmentLightingIrradianceResolveState irradianceState, EEnvironmentLightingPrefilterResolveState prefilterState, bool bUsesSceneSkybox);
    void drawSkyboxComponent(Entity& entity);
    void drawSkyboxStatus(ESkyboxResolveState resolveState);
    void drawSkyboxPreviewSection(const Entity& entity, const SkyboxComponent& skybox);
    void drawSkyboxSourcePreview(const SkyboxPreviewInfo& preview, const SkyboxComponent& skybox);
    void drawSkyboxCubemapPreviewGrid(const SkyboxPreviewInfo& preview);
    void renderScriptProperty(void *propPtr, void *scriptInstancePtr);
    void tryLoadScriptForEditor(void *scriptPtr);
    void testNewRenderInterface(Entity &entity);

    // 兜底：用纯反射 UI 绘制一个已知类型和实例指针的组件，避免重复渲染手写过的类型。
    // 被 drawReflectedFallbackComponents 调用，遍历 ECSRegistry 中所有已注册组件类型。
    void drawReflectedFallbackComponents(Entity &entity);
    void drawReflectedFallbackComponents(const std::vector<Entity*> &entities);
    void drawReflectedFallbackOne(const std::string &name,
                                  type_index_t typeIndex,
                                  std::vector<void*> &instances,
                                  const std::vector<Entity*> &entities);

    /// Copy the value of every modified reflected property (path recorded by
    /// RenderContext) from the first instance to all other instances.
    void applyModificationsToInstances(const std::vector<RenderModificationRecord> &modifications,
                                       type_index_t                               rootTypeIndex,
                                       const std::vector<void*> &                 instances);

    // 画一个通用的「组件分节」标题：Separator + TreeNode + 右上角 "+" 弹出 Remove 菜单。
    // body 在 TreeNode 展开时执行；onRemove 在用户点了 Remove Component 时执行。
    // componentWrapper<T> 与 drawReflectedFallbackOne 共用这里，保证 header 一致。
    template <typename BodyFn, typename RemoveFn>
    void componentSectionShell(const std::string &label, const void *id, bool bCanRemove, BodyFn body, RemoveFn onRemove)
    {
        const auto treeNodeFlags = ImGuiTreeNodeFlags_DefaultOpen |
                                   ImGuiTreeNodeFlags_AllowOverlap |
                                   ImGuiTreeNodeFlags_SpanAvailWidth |
                                   ImGuiTreeNodeFlags_FramePadding |
                                   ImGuiTreeNodeFlags_Framed;

        ImVec2 contentRegionAvailable = ImGui::GetContentRegionAvail();

        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2{4, 4});
        ImGui::Separator();
        ImGui::PopStyleVar(1);

        ImGui::PushID(id);

        bool  bOpen      = ImGui::TreeNodeEx(label.c_str(), treeNodeFlags, "%s", label.c_str());
        float lineHeight = ImGui::GetFont()->LegacySize + ImGui::GetStyle().FramePadding.y * 2.f;
        ImGui::SameLine(contentRegionAvailable.x - lineHeight * 0.5f);

        if (ImGui::Button("+", ImVec2{lineHeight, lineHeight})) {
            ImGui::OpenPopup("ComponentSettings");
        }

        bool bRemoveComponent = false;
        if (ImGui::BeginPopup("ComponentSettings")) {
            if (!bCanRemove) {
                ImGui::BeginDisabled();
                ImGui::MenuItem("Remove Component");
                ImGui::EndDisabled();
                ImGui::TextDisabled("Managed by light component linkage");
            }
            else if (ImGui::MenuItem("Remove Component")) {
                bRemoveComponent = true;
            }
            ImGui::EndPopup();
        }

        if (bOpen) {
            body();
            ImGui::TreePop();
        }

        if (bRemoveComponent) {
            onRemove();
        }

        ImGui::PopID();
    }

    template <typename T, typename Fn>
    void componentWrapper(const std::string &name, Entity &entity, Fn impl)
    {
        if (!entity.hasComponent<T>()) {
            return;
        }
        auto *component = entity.getComponent<T>();
        componentSectionShell(
            name,
            static_cast<const void *>(name.c_str()),
            canRemoveComponent(entity, ya::type_index_v<T>),
            [&] { impl(component); },
            [&] { entity.removeComponent<T>(); });
    }

    /// Shared reflected component section: renders the primary instance, then
    /// propagates every modified property path to all instances and notifies
    /// the caller with the full instance list (single and multi selection).
    template <typename T, typename Fn>
    void drawReflectedComponents(const std::string &name, const std::vector<Entity*> &entities, Fn onComponentDirty)
    {
        std::vector<T*> instances;
        instances.reserve(entities.size());
        for (Entity *entity : entities) {
            if (!entity || !entity->isValid()) {
                return;
            }
            if (!entity->hasComponent<T>()) {
                return; // Not shared by every selection -> skip the section.
            }
            T *component = entity->getComponent<T>();
            instances.push_back(component);
        }
        if (instances.empty()) {
            return;
        }

        const bool bCanRemove = std::all_of(entities.begin(), entities.end(), [this](Entity *entity) {
            return canRemoveComponent(*entity, ya::type_index_v<T>);
        });

        componentSectionShell(
            name,
            static_cast<const void *>(name.c_str()),
            bCanRemove,
            [&] {
                auto typeIndex = ya::type_index_v<T>;
                auto cls       = ClassRegistry::instance().getClass(typeIndex);
                if (!cls) {
                    return;
                }
                ya::RenderContext ctx;
                ctx.beginInstance(instances.front());
                ya::renderReflectedType(name, typeIndex, instances.front(), ctx, 0);
                if (ctx.hasModifications() && instances.size() > 1) {
                    applyModificationsToInstances(ctx.modifications, typeIndex,
                                                  std::vector<void *>(instances.begin(), instances.end()));
                }
                onComponentDirty(instances, ctx);
            },
            [&] {
                for (Entity *entity : entities) {
                    entity->removeComponent<T>();
                }
            });
    }

    template <typename T, typename Fn>
    void drawReflectedComponent(const std::string &name, Entity &entity, Fn onComponentDirty)
    {
        drawReflectedComponents<T>(name, {&entity}, [onComponentDirty](std::vector<T*> &instances, const ya::RenderContext &ctx) {
            for (T *component : instances) {
                if constexpr (std::is_invocable_v<Fn, T *, const ya::RenderContext &>) {
                    onComponentDirty(component, ctx);
                }
                else if constexpr (std::is_invocable_v<Fn, T *>) {
                    if (ctx.hasModifications()) {
                        onComponentDirty(component);
                    }
                }
            }
        });
    }


    template <typename T, typename UIFunction>
    void drawComponent(const std::string &name, Entity entity, UIFunction uiFunc)
    {
        componentWrapper<T>(name, entity, [this, &uiFunc](T *component) {
            uiFunc(component);
        });
    }

    // All material components share the same dirty-handling pattern: forward
    // modified property paths to onPropertiesChanged and expose an
    // "Invalidate" button. `invalidateButtonId` disambiguates the button label
    // when ImGui IDs would otherwise collide across multiple material sections.
    template <typename T>
    void drawMaterialComponent(const std::string &name, const std::vector<Entity*> &entities, const char *invalidateButtonId = "Invalidate")
    {
        drawReflectedComponents<T>(name, entities, [invalidateButtonId](std::vector<T*> &materials, const ya::RenderContext &ctx) {
            if (ctx.hasModifications()) {
                for (T *mat : materials) {
                    mat->onPropertiesChanged(ctx.getModificationPaths());
                }
            }
            if (ImGui::Button(invalidateButtonId)) {
                for (T *mat : materials) {
                    mat->invalidate();
                }
            }
        });
    }
};

} // namespace ya
