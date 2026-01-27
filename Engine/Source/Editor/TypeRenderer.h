#pragma once

#include "reflects-core/lib.h"

#include "Core/Reflection/ContainerProperty.h"
#include "Core/Reflection/MetadataSupport.h"
#include <functional>
#include <string>
#include <unordered_map>

#include <imgui.h>
namespace ya
{

// ============================================================================
// MARK: Property Render Context
// ============================================================================

/**
 * @brief 属性渲染上下文 - 缓存属性的渲染相关信息
 *
 * 职责：
 * - 缓存属性的元数据（manipulateSpec）
 * - 缓存容器访问器
 * - 提供格式化后的显示名称
 */
struct PropertyRenderContext
{
    bool                            isContainer       = false;
    bool                            bPointer          = false; ///< True if property is a pointer type
    uint32_t                        pointeeTypeIndex  = 0;     ///< Type index of pointee (if bPointer)
    reflection::IContainerProperty *containerAccessor = nullptr;
    std::string                     prettyName;

    // 元数据（仅对基础类型有效）
    reflection::Meta::ManipulateSpec manipulateSpec;
    bool                             bColor = false;

    // 辅助方法：从 Property 提取元数据
    static PropertyRenderContext createFrom(const Property &prop, const std::string &propName);
};

// ============================================================================
// MARK: Type Renderer
// ============================================================================

/**
 * @brief 类型渲染器 - 定义特定类型的 ImGui 渲染逻辑
 *
 * 职责：
 * - 封装类型的 ImGui 渲染函数
 * - 接收实例指针和渲染上下文作为参数
 *
 * 注意：不持有任何状态，只是纯函数封装
 */
struct TypeRenderer
{
    // using RenderFunc    = bool (*)(void *instance, const PropertyRenderContext &ctx);
    using RenderFunc = std::function<bool(void *, const PropertyRenderContext &)>;

    std::string typeName;
    RenderFunc  renderFunc = nullptr;

    bool render(void *instance, const PropertyRenderContext &ctx) const
    {
        return renderFunc ? renderFunc(instance, ctx) : false;
    }
};

// ============================================================================
// MARK: Type Render Registry (Singleton)
// ============================================================================

/**
 * @brief 类型渲染器注册表 - 管理所有类型的渲染器
 *
 * 职责：
 * - 注册类型渲染器
 * - 查询类型渲染器
 * - 提供默认渲染器
 */
class TypeRenderRegistry
{
  public:
    static TypeRenderRegistry &instance();

    void registerRenderer(uint32_t typeIndex, TypeRenderer renderer)
    {
        _renderers[typeIndex] = renderer;
    }

    const TypeRenderer *getRenderer(uint32_t typeIndex) const
    {
        auto it = _renderers.find(typeIndex);
        return it != _renderers.end() ? &it->second : nullptr;
    }

    void clear() { _renderers.clear(); }

  private:
    TypeRenderRegistry() = default;
    std::unordered_map<uint32_t, TypeRenderer> _renderers;
};

// ============================================================================
// MARK: Type Rendering Functions
// ============================================================================

/**
 * @brief 递归渲染反射类型
 * @param name 显示名称
 * @param typeIndex 类型索引
 * @param instance 实例指针
 * @param depth 递归深度
 * @param ctx 可选的属性渲染上下文，如果提供则直接使用，避免字符串查找
 * @return 是否发生修改
 */
bool renderReflectedType(const std::string &name, uint32_t typeIndex, void *instance, int depth = 0, const PropertyRenderContext *ctx = nullptr);

/**
 * @brief 注册内置类型渲染器（int, float, string, vec3, vec4 等）
 */
void registerBuiltinTypeRenderers();

bool pathWrapper(void *instance, const PropertyRenderContext &ctx, auto internal);



bool renderPathPicker(std::string path, const std::string &typeName, auto internal)
{
    bool modified = false;

    // Display current path
    std::string displayPath = path.empty() ? "[No Path]" : path;

    // Path input field (read-only display)
    ImGui::Text("%s:", typeName.c_str());
    ImGui::SameLine();
    ImGui::SetNextItemWidth(-80); // Leave space for button

    char buffer[256];
    strncpy(buffer, displayPath.c_str(), sizeof(buffer) - 1);
    buffer[sizeof(buffer) - 1] = '\0';

    if (ImGui::InputText(("##" + typeName).c_str(), buffer, sizeof(buffer))) {
        path     = buffer;
        modified = true;
    }

    // Browse button - access FilePicker through App::get()->_editorLayer
    ImGui::SameLine();
    if (ImGui::Button(("Browse##" + typeName).c_str())) {
        internal(path);
    }

    return modified;
}

struct EditorLayer *getEditor();
struct FilePicker  *getFilePicker();

} // namespace ya
