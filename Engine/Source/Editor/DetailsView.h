#pragma once

#include "Core/TypeIndex.h"
#include "ECS/Entity.h"
#include "FilePicker.h"
#include "TypeRenderer.h"
#include <ImGui.h>
#include <sol/sol.hpp>
#include <type_traits>

namespace ya
{

struct Scene;
struct EditorLayer;
struct LuaScriptComponent;
struct EnvironmentLightingComponent;
struct SkyboxComponent;
struct SkyboxPreviewInfo;
struct Texture;

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

  public:
    DetailsView(EditorLayer *owner);

    void onImGuiRender();

  private:
    void drawComponents(Entity &entity);
    void drawAddComponentButton(Entity &entity); // Add component popup
    void drawEnvironmentLightingComponent(Entity& entity);
    void drawEnvironmentLightingStatus(const EnvironmentLightingComponent& environmentLighting);
    void drawSkyboxComponent(Entity& entity);
    void drawSkyboxStatus(const SkyboxComponent& skybox);
    void drawSkyboxPreviewSection(const Entity& entity, const SkyboxComponent& skybox);
    void drawSkyboxSourcePreview(const SkyboxPreviewInfo& preview, const SkyboxComponent& skybox);
    void drawSkyboxCubemapPreviewGrid(const SkyboxPreviewInfo& preview);
    void renderScriptProperty(void *propPtr, void *scriptInstancePtr);
    void tryLoadScriptForEditor(void *scriptPtr);
    void testNewRenderInterface(Entity &entity);

    // 兜底：用纯反射 UI 绘制一个已知类型和实例指针的组件，避免重复渲染手写过的类型。
    // 被 drawReflectedFallbackComponents 调用，遍历 ECSRegistry 中所有已注册组件类型。
    void drawReflectedFallbackComponents(Entity &entity);
    void drawReflectedFallbackOne(const std::string &name, type_index_t typeIndex, void *component, Entity &entity);

    // 画一个通用的「组件分节」标题：Separator + TreeNode + 右上角 "+" 弹出 Remove 菜单。
    // body 在 TreeNode 展开时执行；onRemove 在用户点了 Remove Component 时执行。
    // componentWrapper<T> 与 drawReflectedFallbackOne 共用这里，保证 header 一致。
    template <typename BodyFn, typename RemoveFn>
    void componentSectionShell(const std::string &label, const void *id, BodyFn body, RemoveFn onRemove)
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
            if (ImGui::MenuItem("Remove Component")) {
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
            [&] { impl(component); },
            [&] { entity.removeComponent<T>(); });
    }

    template <typename T>
    void drawReflectedComponent(const std::string &name, Entity &entity, auto &&onComponentDirty)
    {
        // YA_PROFILE_SCOPE(std::format("DetailsView::drawReflectedComponent<{}>", name).c_str());
        componentWrapper<T>(name, entity, [this, &onComponentDirty, &name](T *component) {
            auto typeIndex = ya::type_index_v<T>;
            auto cls       = ClassRegistry::instance().getClass(typeIndex);
            if (!cls) {
                return;
            }
            ya::RenderContext ctx;
            ctx.beginInstance(component);
            ya::renderReflectedType(name, typeIndex, component, ctx, 0);
            bool bComponentDirty = ctx.hasModifications();
            if constexpr (std::is_invocable_v<decltype(onComponentDirty), T *, const ya::RenderContext &>) {
                onComponentDirty(component, ctx);
            }
            else if constexpr (std::is_invocable_v<decltype(onComponentDirty), T *>) {
                if (bComponentDirty) {
                    onComponentDirty(component);
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
    // modified property paths to onEditorPropertiesChanged and expose an
    // "Invalidate" button. `invalidateButtonId` disambiguates the button label
    // when ImGui IDs would otherwise collide across multiple material sections.
    template <typename T>
    void drawMaterialComponent(const std::string &name, Entity &entity, const char *invalidateButtonId = "Invalidate")
    {
        drawReflectedComponent<T>(name, entity, [invalidateButtonId](T *mat, const ya::RenderContext &ctx) {
            if (ctx.hasModifications()) {
                mat->onEditorPropertiesChanged(ctx.getModificationPaths());
            }
            if (ImGui::Button(invalidateButtonId)) {
                mat->invalidate();
            }
        });
    }
};

} // namespace ya
