#include "TypeRenderer.h"

#include "ContainerPropertyRenderer.h"
#include "Core/Debug/Instrumentor.h"
#include "Core/TypeIndex.h"
#include "ReflectionCache.h"
#include "reflects-core/lib.h"

#include <glm/glm.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <imgui.h>

namespace ya
{

// ============================================================================
// PropertyRenderContext Implementation
// ============================================================================

PropertyRenderContext PropertyRenderContext::createFrom(const Property &prop, const std::string &propName)
{
    PropertyRenderContext ctx;

    // 检查是否为容器
    ctx.isContainer = ::ya::reflection::PropertyContainerHelper::isContainer(prop);
    if (ctx.isContainer) {
        ctx.containerAccessor = ::ya::reflection::PropertyContainerHelper::getContainerAccessor(prop);
    }
    else {
        // 提取元数据
        auto metadata = prop.getMetadata();
        if (metadata.hasMeta(ya::reflection::Meta::ManipulateSpec::name)) {
            ctx.manipulateSpec =
                metadata.get<ya::reflection::Meta::ManipulateSpec>(ya::reflection::Meta::ManipulateSpec::name);
        }
        if (metadata.hasMeta(ya::reflection::Meta::Color)) {
            ctx.bColor = metadata.get<bool>(ya::reflection::Meta::Color);
        }
    }

    // 格式化显示名称（移除前缀 _ 和 m_）
    auto sv = std::string_view(propName);
    if (sv.starts_with("_")) {
        sv.remove_prefix(1);
    }
    if (sv.starts_with("m_")) {
        sv.remove_prefix(2);
    }
    ctx.prettyName = std::string(sv);

    return ctx;
}

// ============================================================================
// TypeRenderRegistry Implementation
// ============================================================================

TypeRenderRegistry &TypeRenderRegistry::instance()
{
    static TypeRenderRegistry inst;
    return inst;
}

// ============================================================================
// Type Rendering Functions
// ============================================================================

// 递归深度限制
static constexpr int MAX_RECURSION_DEPTH = 10;

bool renderReflectedType(const std::string &name, uint32_t typeIndex, void *instancePtr, int depth, const PropertyRenderContext *ctx)
{
    YA_PROFILE_SCOPE(std::format("renderReflectedType, {}, typeIndex: {}", name, typeIndex));
    if (depth >= MAX_RECURSION_DEPTH) {
        ImGui::TextDisabled("%s: [max recursion depth reached]", name.c_str());
        return false;
    }
    auto cache = getOrCreateReflectionCache(typeIndex);

    // 尝试使用类型渲染器
    if (auto *renderer = TypeRenderRegistry::instance().getRenderer(typeIndex)) {
        // 如果调用方提供了 ctx，直接使用；否则使用空上下文
        PropertyRenderContext emptyCtx;
        emptyCtx.prettyName = name; // 至少设置显示名称
        return renderer->render(instancePtr, ctx ? *ctx : emptyCtx);
    }

    bool bModified = false;

    if (cache && cache->componentClassPtr && cache->propertyCount > 0) {
        ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_FramePadding;

        auto iterateChildren = [&]() {
            for (auto &[propName, prop] : cache->componentClassPtr->properties) {
                auto subPropInstancePtr = prop.addressGetterMutable(instancePtr);

                // 从缓存获取属性渲染上下文
                const auto &propCtx     = cache->propertyContexts[propName];
                const bool  isContainer = propCtx.isContainer;
                const auto &prettyName  = propCtx.prettyName;

                if (isContainer) {
                    // 容器属性：使用容器渲染器
                    bool containerModified = ya::editor::ContainerPropertyRenderer::renderContainer(
                        prettyName,
                        prop,
                        subPropInstancePtr,
                        [depth](const std::string &label, void *elementPtr, uint32_t elementTypeIndex) -> bool {
                            return renderReflectedType(label, elementTypeIndex, elementPtr, depth + 2);
                        });
                    if (containerModified) {
                        bModified = true;
                    }
                }
                else {
                    // 普通属性：直接传递 PropertyRenderContext
                    if (renderReflectedType(prettyName, prop.typeIndex, subPropInstancePtr, depth + 1, &propCtx)) {
                        bModified = true;
                    }
                }
            }
        };

        if (depth == 0)
        {
            iterateChildren();
        }
        else {
            // 优化：depth > 1 时默认折叠，减少渲染开销
            ImGuiTreeNodeFlags nodeFlags = (depth > 1) ? flags & ~ImGuiTreeNodeFlags_DefaultOpen : flags;
            bool               isOpen    = ImGui::TreeNodeEx(name.c_str(), nodeFlags, "%s", name.c_str());
            if (isOpen) {
                ImGui::Indent();
                iterateChildren();
                ImGui::Unindent();
                ImGui::TreePop();
            }
        }
    }
    else {
        ImGui::TextDisabled("%s: [unsupported type]", name.c_str());
    }

    return bModified;
}

// ============================================================================
// Builtin Type Renderers Registration
// ============================================================================

void registerBuiltinTypeRenderers()
{
    auto &registry = TypeRenderRegistry::instance();

    // int 类型渲染器
    registry.registerRenderer(
        ya::type_index_v<int>,
        TypeRenderer{
            .typeName   = "int",
            .renderFunc = [](void *instance, const PropertyRenderContext &ctx) -> bool {
                const auto &spec = ctx.manipulateSpec;
                auto        ptr  = static_cast<int *>(instance);
                switch (spec.type) {
                case ya::reflection::Meta::ManipulateSpec::Slider:
                    return ImGui::SliderInt(ctx.prettyName.c_str(), ptr, static_cast<int>(spec.min), static_cast<int>(spec.max));
                case ya::reflection::Meta::ManipulateSpec::Drag:
                    return ImGui::DragInt(ctx.prettyName.c_str(), ptr, static_cast<int>(spec.step));
                case ya::reflection::Meta::ManipulateSpec::Input:
                    return ImGui::InputInt(ctx.prettyName.c_str(), ptr, static_cast<int>(spec.step));
                case ya::reflection::Meta::ManipulateSpec::None:
                    return ImGui::InputInt(ctx.prettyName.c_str(), ptr);
                }
                return false;
            },
        });

    // float 类型渲染器
    registry.registerRenderer(
        ya::type_index_v<float>,
        TypeRenderer{
            .typeName   = "float",
            .renderFunc = [](void *instance, const PropertyRenderContext &ctx) -> bool {
                const auto &spec = ctx.manipulateSpec;
                auto        ptr  = static_cast<float *>(instance);
                switch (spec.type) {
                case ya::reflection::Meta::ManipulateSpec::Slider:
                    return ImGui::SliderFloat(ctx.prettyName.c_str(), ptr, spec.min, spec.max);
                case ya::reflection::Meta::ManipulateSpec::Drag:
                    return ImGui::DragFloat(ctx.prettyName.c_str(), ptr, spec.step);
                case ya::reflection::Meta::ManipulateSpec::Input:
                    return ImGui::InputFloat(ctx.prettyName.c_str(), ptr, spec.step);
                case ya::reflection::Meta::ManipulateSpec::None:
                    return ImGui::DragFloat(ctx.prettyName.c_str(), ptr);
                }
                return false;
            },
        });

    // std::string 类型渲染器
    registry.registerRenderer(
        ya::type_index_v<std::string>,
        TypeRenderer{
            .typeName   = "std::string",
            .renderFunc = [](void *instance, const PropertyRenderContext &ctx) -> bool {
                auto &s = *static_cast<std::string *>(instance);
                char  buf[256];
                std::memcpy(buf, s.c_str(), std::min(s.size() + 1, sizeof(buf)));
                buf[sizeof(buf) - 1] = '\0';

                bool bModified = ImGui::InputText(ctx.prettyName.c_str(), buf, sizeof(buf));
                if (bModified) {
                    s = buf;
                }
                return bModified;
            },
        });

    // glm::vec3 类型渲染器
    registry.registerRenderer(
        ya::type_index_v<glm::vec3>,
        TypeRenderer{
            .typeName   = "glm::vec3",
            .renderFunc = [](void *instance, const PropertyRenderContext &ctx) -> bool {
                auto *ptr = static_cast<float *>(instance);
                if (ctx.bColor) {
                    return ImGui::ColorEdit3(ctx.prettyName.c_str(), ptr);
                }
                return ImGui::DragFloat3(ctx.prettyName.c_str(), ptr);
            },
        });

    // glm::vec4 类型渲染器（颜色）
    registry.registerRenderer(
        ya::type_index_v<glm::vec4>,
        TypeRenderer{
            .typeName   = "glm::vec4",
            .renderFunc = [](void *instance, const PropertyRenderContext &ctx) -> bool {
                float *val = static_cast<float *>(instance);
                if (ctx.bColor) {
                    return ImGui::ColorEdit4(ctx.prettyName.c_str(), val);
                }
                return ImGui::DragFloat4(ctx.prettyName.c_str(), val);
            },
        });
}

} // namespace ya
