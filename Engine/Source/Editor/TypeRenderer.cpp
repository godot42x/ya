#include "TypeRenderer.h"

#include "ContainerPropertyRenderer.h"
#include "Core/App/App.h"
#include "Core/Common/AssetRef.h"
#include "Core/Debug/Instrumentor.h"
#include "Core/System/VirtualFileSystem.h"
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
    if (!cache) {
        ImGui::TextDisabled("%s: [unsupported type]", name.c_str());
        return false;
    }

    // 枚举类型
    if (cache->bEnum && cache->enumMisc.enumPtr) {
        int64_t currentValue = cache->enumMisc.enumPtr->getValue(instancePtr);
        int     currentIndex = cache->enumMisc.valueToPosition[currentValue];

        if (ImGui::Combo(ctx->prettyName.c_str(),
                         &currentIndex,
                         cache->enumMisc.imguiComboString.c_str(),
                         (int)cache->enumMisc.positionToValue.size())) {
            // int64_t newVal = values[static_cast<size_t>(selected)].value;
            int64_t newVal = cache->enumMisc.positionToValue[currentIndex];
            cache->enumMisc.enumPtr->setValue(instancePtr, newVal);
            return true;
        }
        return false;
    }


    if (cache->classPtr) {

        if (cache->propertyCount > 0 || cache->classPtr->parents.size() > 0)
        {

            auto iterateAll = [&]() {
                // 首先递归渲染父类的属性
                auto &registry = ClassRegistry::instance();
                for (auto parentTypeId : cache->classPtr->parents) {
                    // 获取父类指针
                    void *parentPtr = cache->classPtr->getParentPointer(instancePtr, parentTypeId);
                    if (!parentPtr) {
                        continue;
                    }

                    // 获取父类的 Class 信息
                    if (auto *parentClass = registry.getClass(parentTypeId)) {
                        // 递归渲染父类（使用父类的 typeIndex 和指针）
                        if (renderReflectedType(parentClass->getName(), parentTypeId, parentPtr, depth + 1, nullptr)) {
                            bModified = true;
                        }
                    }
                }

                // 然后渲染当前类的属性
                for (auto &[propName, prop] : cache->classPtr->properties) {
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
                iterateAll();
                return bModified;
            }
            else {
                ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_FramePadding;
                if (depth > 1) {
                    flags = flags & ~ImGuiTreeNodeFlags_DefaultOpen;
                }
                // 优化：depth > 1 时默认折叠，减少渲染开销
                bool isOpen = ImGui::TreeNodeEx(name.c_str(), flags, "%s", name.c_str());
                if (isOpen) {
                    ImGui::Indent();
                    iterateAll();
                    ImGui::Unindent();
                    ImGui::TreePop();
                }
                return bModified;
            }
        }
    }

    ImGui::TextDisabled("%s: [unsupported type]", name.c_str());
    return bModified;
}

// ============================================================================
// Builtin Type Renderers Registration
// ============================================================================



void registerBuiltinTypeRenderers()
{
    auto &registry = TypeRenderRegistry::instance();

    static auto integerRenderFunc = [](int &value, const PropertyRenderContext &ctx) -> bool {
        const auto &spec = ctx.manipulateSpec;
        switch (spec.type) {
        case ya::reflection::Meta::ManipulateSpec::Slider:
            return ImGui::SliderInt(ctx.prettyName.c_str(), &value, static_cast<int>(spec.min), static_cast<int>(spec.max));
        case ya::reflection::Meta::ManipulateSpec::Drag:
            return ImGui::DragInt(ctx.prettyName.c_str(), &value, static_cast<int>(spec.step));
        case ya::reflection::Meta::ManipulateSpec::Input:
            return ImGui::InputInt(ctx.prettyName.c_str(), &value, static_cast<int>(spec.step));
        case ya::reflection::Meta::ManipulateSpec::None:
            return ImGui::InputInt(ctx.prettyName.c_str(), &value);
        }
        return false;
    };

    // int 类型渲染器
    registry.registerRenderer(
        ya::type_index_v<int>,
        TypeRenderer{
            .typeName   = "int",
            .renderFunc = [](void *instance, const PropertyRenderContext &ctx) -> bool {
                return integerRenderFunc(*static_cast<int *>(instance), ctx);
            },
        });
    registry.registerRenderer(
        ya::type_index_v<uint32_t>,
        TypeRenderer{
            .typeName   = "uint32_t",
            .renderFunc = [](void *instance, const PropertyRenderContext &ctx) -> bool {
                auto &value    = *static_cast<uint32_t *>(instance);
                int   temp     = static_cast<int>(value);
                bool  modified = integerRenderFunc(temp, ctx);
                if (modified) {
                    value = static_cast<uint32_t>(temp);
                }
                return modified;
            },
        });
    registry.registerRenderer(
        ya::type_index_v<int32_t>,
        TypeRenderer{
            .typeName   = "int32_t",
            .renderFunc = [](void *instance, const PropertyRenderContext &ctx) -> bool {
                auto &value    = *static_cast<int32_t *>(instance);
                int   temp     = static_cast<int>(value);
                bool  modified = integerRenderFunc(temp, ctx);
                if (modified) {
                    value = static_cast<int32_t>(temp);
                }
                return modified;
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
    registry.registerRenderer(
        ya::type_index_v<AssetRefBase>,
        TypeRenderer{
            .typeName   = "AssetRefBase",
            .renderFunc = [](void *instance, const PropertyRenderContext &ctx) -> bool {
                auto &assetRef = *static_cast<AssetRefBase *>(instance);
                bool  modified = false;

                // Display current path
                std::string displayPath = assetRef._path.empty() ? "[No Model]" : assetRef._path;

                // Path input field (read-only display)
                ImGui::Text("%s:", ctx.prettyName.c_str());
                ImGui::SameLine();
                ImGui::SetNextItemWidth(-80); // Leave space for button

                char buffer[256];
                strncpy(buffer, displayPath.c_str(), sizeof(buffer) - 1);
                buffer[sizeof(buffer) - 1] = '\0';

                if (ImGui::InputText(("##" + ctx.prettyName).c_str(), buffer, sizeof(buffer))) {
                    assetRef._path = buffer;
                    modified       = true;
                }

                // Browse button - access FilePicker through App::get()->_editorLayer
                ImGui::SameLine();
                if (ImGui::Button(("Browse##" + ctx.prettyName).c_str())) {
                    if (auto *app = App::get()) {
                        if (auto *editorLayer = app->_editorLayer) {
                            editorLayer->_filePicker.openModelPicker(
                                assetRef._path,
                                [&assetRef](const std::string &newPath) {
                                    auto p         = VFS::get()->relativeTo(newPath, VFS::get()->getProjectRoot()).string();
                                    assetRef._path = p;
                                    assetRef.invalidate();
                                });
                        }
                    }
                }

                return modified;
            },
        });
}

} // namespace ya
