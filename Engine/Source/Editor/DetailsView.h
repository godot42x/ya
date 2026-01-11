#pragma once

#include "ECS/Entity.h"
#include "FilePicker.h"
#include <imgui.h>
#include <memory>
#include <sol/sol.hpp>
#include <unordered_map>

#include "Core/Debug/Instrumentor.h"


struct Class;
struct Property;
namespace ya
{

// Constants
constexpr size_t DETAILS_SCRIPT_INPUT_BUFFER_SIZE = 256;

struct Scene;
struct EditorLayer;
struct LuaScriptComponent;

struct ReflectionCache
{
    Class   *componentClassPtr;
    size_t   propertyCount = 0;
    uint32_t typeIndex     = 0;

    bool isValid(uint32_t ti) const { return ti == typeIndex && componentClassPtr != nullptr; }
};

struct DetailsView
{
  private:
    EditorLayer *_owner;

    // 编辑器专用 Lua 状态（用于预览属性）
    sol::state _editorLua;
    bool       _editorLuaInitialized = false;

    // 文件选择器
    FilePicker _filePicker;

    // 反射缓存
    std::unordered_map<size_t, ReflectionCache> _reflectionCache;
    struct PropRenderContext
    {
        const std::string &name;
    };
    std::unordered_map<uint32_t, bool (*)(void *instance, const PropRenderContext &ctx)> _typeRender;

    // 递归深度追踪（防止无限递归）
    int                  _recursionDepth     = 0;
    static constexpr int MAX_RECURSION_DEPTH = 10;

  public:
    DetailsView(EditorLayer *owner);

    void onImGuiRender();

  private:
    void drawComponents(Entity &entity);
    void renderScriptProperty(void *propPtr, void *scriptInstancePtr);
    void tryLoadScriptForEditor(void *scriptPtr);

    // 递归反射属性渲染
    bool             renderReflectedType(const std::string &name, uint32_t typeIndex, void *instance, int depth = 0);
    ReflectionCache *getOrCreateReflectionCache(uint32_t typeIndex);


    template <typename T, typename Fn>
    void componentWrapper(const std::string &name, Entity &entity, Fn impl)
    {
        const auto treeNodeFlags = ImGuiTreeNodeFlags_DefaultOpen |
                                   ImGuiTreeNodeFlags_AllowOverlap |
                                   ImGuiTreeNodeFlags_SpanAvailWidth |
                                   ImGuiTreeNodeFlags_FramePadding |
                                   ImGuiTreeNodeFlags_Framed;

        if (!entity.hasComponent<T>()) {
            return;
        }

        auto  *component              = entity.getComponent<T>();
        ImVec2 contentRegionAvailable = ImGui::GetContentRegionAvail();

        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2{4, 4});
        // gap between component section
        ImGui::Separator();
        ImGui::PopStyleVar(1);

        bool  bOpen      = ImGui::TreeNodeEx(name.c_str(), treeNodeFlags, "%s", name.c_str());
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
            impl(component);
            ImGui::TreePop();
        }

        if (bRemoveComponent) {
            entity.removeComponent<T>();
        }
    }

    template <typename T>
    void drawReflectedComponent(const std::string &name, Entity &entity, auto &&onComponentDirty)
    {
        // YA_PROFILE_SCOPE(std::format("DetailsView::drawReflectedComponent<{}>", name).c_str());
        componentWrapper<T>(name, entity, [this, &onComponentDirty, &name](T *component) {
            auto typeIndex = ya::type_index_v<T>;
            auto cls       = ClassRegistry::instance().getClass(ya::type_index_v<T>);
            if (!cls) {
                return;
            }
            bool bComponentDirty = renderReflectedType(name, typeIndex, component, 0);
            if constexpr (std::is_invocable_v<decltype(onComponentDirty), T *>) {
                if (bComponentDirty) {
                    onComponentDirty(component);
                }
            }
        });
    }

    // void drawReflectedComponentByTypeIndex(const std::string &name, uint32_t typeIndex, Entity &entity, void *componentInstance)
    // {
    //     componentWrapper<void>(name, entity, [this, typeIndex, &name](void *component) {
    //         renderReflectedType(name, typeIndex, component, 0);
    //     });
    // }

    template <typename T, typename UIFunction>
    void drawComponent(const std::string &name, Entity entity, UIFunction uiFunc)
    {
        componentWrapper<T>(name, entity, [this, &uiFunc](T *component) {
            uiFunc(component);
        });
    }
};

} // namespace ya
