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
// TypeRenderRegistry Implementation
// ============================================================================

TypeRenderRegistry &TypeRenderRegistry::instance()
{
    static TypeRenderRegistry inst;
    return inst;
}

// ============================================================================
// Type Rendering Functions - New Implementation with Context
void renderReflectedType(const std::string &name, uint32_t typeIndex, void *instance, RenderContext &ctx, int depth, const PropertyRenderContext *propRenderCache)
{
    YA_PROFILE_SCOPE(std::format("renderReflectedType(ctx), {}, typeIndex: {}", name, typeIndex));

    // Use ScopedPath to automatically track property path
    RenderContext::ScopedPath scopedPath(ctx, name);

    if (depth >= MAX_RECURSION_DEPTH) {
        ImGui::TextDisabled("%s: [max recursion depth reached]", name.c_str());
        return;
    }
    auto cache = getOrCreateReflectionCache(typeIndex);

    if (!cache) {
        ImGui::TextDisabled("%s: [unsupported type]", name.c_str());
        return;
    }


    // 如果没有类缓存，尝试使用类型渲染器
    if (auto *renderer = TypeRenderRegistry::instance().getRenderer(typeIndex)) {
        renderer->render(instance, propRenderCache ? *propRenderCache : PropertyRenderContext{}, ctx);
        return;
    }

    // 枚举类型
    if (cache->bEnum && cache->enumMisc.enumPtr) {
        int64_t currentValue = cache->enumMisc.enumPtr->getValue(instance);
        int     currentIndex = cache->enumMisc.valueToPosition[currentValue];

        if (ImGui::Combo(propRenderCache ? propRenderCache->prettyName.c_str() : name.c_str(),
                         &currentIndex,
                         cache->enumMisc.imguiComboString.c_str(),
                         (int)cache->enumMisc.positionToValue.size())) {
            int64_t newVal = cache->enumMisc.positionToValue[currentIndex];
            cache->enumMisc.enumPtr->setValue(instance, newVal);
            ctx.addModification(nullptr, name);
        }
        return;
    }


    if (cache->classPtr)
    {
        if (cache->propertyCount > 0 || cache->classPtr->parents.size() > 0)
        {
            auto iterateAll = [&]() {
                // 首先递归渲染父类的属性
                auto &registry = ClassRegistry::instance();
                for (auto parentTypeId : cache->classPtr->parents) {
                    // 获取父类指针
                    void *parentPtr = cache->classPtr->getParentPointer(instance, parentTypeId);
                    if (!parentPtr) {
                        continue;
                    }

                    // 获取父类的 Class 信息
                    if (auto *parentClass = registry.getClass(parentTypeId)) {
                        // 递归渲染父类（使用父类的 typeIndex 和指针）
                        renderReflectedType(parentClass->getName(), parentTypeId, parentPtr, ctx, depth + 1, nullptr);
                    }
                }

                // 然后渲染当前类的属性
                for (auto &[propName, prop] : cache->classPtr->properties) {
                    auto subPropInstancePtr = prop.addressGetterMutable(instance);

                    // 从缓存获取属性渲染上下文
                    const auto &propCtxCache = cache->propertyContexts[propName];
                    const bool  isContainer  = propCtxCache.isContainer;
                    const bool  isPointer    = propCtxCache.bPointer;
                    const auto &prettyName   = propCtxCache.prettyName;

                    if (isContainer) {
                        // 容器属性：使用容器渲染器
                        // TODO: ContainerPropertyRenderer should also use RenderContext
                        ya::editor::ContainerPropertyRenderer::renderContainer(
                            prettyName,
                            prop,
                            subPropInstancePtr,
                            depth + 2);
                    }
                    else if (isPointer && propCtxCache.pointeeTypeIndex != 0) {
                        // Pointer property: dereference and render pointee object
                        void **ptrLocation = static_cast<void **>(subPropInstancePtr);
                        void  *pointee     = ptrLocation ? *ptrLocation : nullptr;

                        if (pointee) {
                            // Has valid pointee - render it with indirection indicator
                            std::string ptrLabel = prettyName + " (->)";
                            renderReflectedType(ptrLabel, propCtxCache.pointeeTypeIndex, pointee, ctx, depth + 1, nullptr);
                        }
                        else {
                            // Null pointer - show placeholder
                            ImGui::TextDisabled("%s: [null]", prettyName.c_str());
                        }
                    }
                    else {
                        // 普通属性：直接传递 PropertyRenderContext
                        renderReflectedType(prettyName, prop.typeIndex, subPropInstancePtr, ctx, depth + 1, &propCtxCache);
                    }
                }
            };

            if (depth == 0)
            {
                iterateAll();
                return;
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
                return;
            }
        }
    }

    ImGui::TextDisabled("%s: [unsupported type]", name.c_str());
}

// ============================================================================
// Builtin Type Renderers Registration
// ============================================================================

// Helper function for integer types rendering
static bool integerRenderFunc(int &value, const PropertyRenderContext &propCtx)
{
    const auto &spec = propCtx.manipulateSpec;
    switch (spec.type) {
    case ya::reflection::Meta::ManipulateSpec::Slider:
        return ImGui::SliderInt(propCtx.prettyName.c_str(), &value, static_cast<int>(spec.min), static_cast<int>(spec.max));
    case ya::reflection::Meta::ManipulateSpec::Drag:
        return ImGui::DragInt(propCtx.prettyName.c_str(), &value, static_cast<int>(spec.step), 100);
    case ya::reflection::Meta::ManipulateSpec::Input:
        return ImGui::InputInt(propCtx.prettyName.c_str(), &value, static_cast<int>(spec.step), 100);
    case ya::reflection::Meta::ManipulateSpec::None:
        return ImGui::InputInt(propCtx.prettyName.c_str(), &value, static_cast<int>(spec.step), 100);
    }
    return false;
}

void registerBuiltinTypeRenderers()
{
    auto &registry = TypeRenderRegistry::instance();

    // int 类型渲染器
    registry.registerRenderer(
        ya::type_index_v<int>,
        TypeRenderer{
            .typeName   = "int",
            .renderFunc = [](void *instance, const PropertyRenderContext &propCtx, RenderContext &ctx) {
                auto ptr = static_cast<int *>(instance);
                if (integerRenderFunc(*ptr, propCtx)) {
                    ctx.pushModified();
                }
            },
        });

    registry.registerRenderer(
        ya::type_index_v<uint32_t>,
        TypeRenderer{
            .typeName   = "uint32_t",
            .renderFunc = [](void *instance, const PropertyRenderContext &propCtx, RenderContext &ctx) {
                auto &value = *static_cast<uint32_t *>(instance);
                int   temp  = static_cast<int>(value);
                if (integerRenderFunc(temp, propCtx)) {
                    value = static_cast<uint32_t>(temp);
                    ctx.pushModified();
                }
            },
        });

    registry.registerRenderer(
        ya::type_index_v<int32_t>,
        TypeRenderer{
            .typeName   = "int32_t",
            .renderFunc = [](void *instance, const PropertyRenderContext &propCtx, RenderContext &ctx) {
                auto &value = *static_cast<int32_t *>(instance);
                int   temp  = static_cast<int>(value);
                if (integerRenderFunc(temp, propCtx)) {
                    value = static_cast<int32_t>(temp);
                    ctx.pushModified();
                }
            },
        });

    registry.registerRenderer(
        ya::type_index_v<uint64_t>,
        TypeRenderer{
            .typeName   = "uint64_t",
            .renderFunc = [](void *instance, const PropertyRenderContext &propCtx, RenderContext &ctx) {
                auto &value = *static_cast<uint64_t *>(instance);
                int   temp  = static_cast<int>(value);
                if (integerRenderFunc(temp, propCtx)) {
                    value = static_cast<uint64_t>(temp);
                    ctx.pushModified();
                }
            },
        });

    // bool
    registry.registerRenderer(
        ya::type_index_v<bool>,
        TypeRenderer{
            .typeName   = "bool",
            .renderFunc = [](void *instance, const PropertyRenderContext &propCtx, RenderContext &ctx) {
                if (ImGui::Checkbox(propCtx.prettyName.c_str(), static_cast<bool *>(instance))) {
                    ctx.pushModified();
                }
            },
        });

    // float 类型渲染器
    registry.registerRenderer(
        ya::type_index_v<float>,
        TypeRenderer{
            .typeName   = "float",
            .renderFunc = [](void *instance, const PropertyRenderContext &propCtx, RenderContext &ctx) {
                const auto &spec     = propCtx.manipulateSpec;
                auto        ptr      = static_cast<float *>(instance);
                bool        modified = false;
                switch (spec.type) {
                case ya::reflection::Meta::ManipulateSpec::Slider:
                    modified = ImGui::SliderFloat(propCtx.prettyName.c_str(), ptr, spec.min, spec.max);
                    break;
                case ya::reflection::Meta::ManipulateSpec::Drag:
                    modified = ImGui::DragFloat(propCtx.prettyName.c_str(), ptr, spec.step);
                    break;
                case ya::reflection::Meta::ManipulateSpec::Input:
                    modified = ImGui::InputFloat(propCtx.prettyName.c_str(), ptr, spec.step);
                    break;
                case ya::reflection::Meta::ManipulateSpec::None:
                    modified = ImGui::DragFloat(propCtx.prettyName.c_str(), ptr);
                    break;
                }
                if (modified) {
                    ctx.pushModified();
                }
            },
        });

    // std::string 类型渲染器
    registry.registerRenderer(
        ya::type_index_v<std::string>,
        TypeRenderer{
            .typeName   = "std::string",
            .renderFunc = [](void *instance, const PropertyRenderContext &propCtx, RenderContext &ctx) {
                auto &s = *static_cast<std::string *>(instance);
                char  buf[256];
                std::memcpy(buf, s.c_str(), std::min(s.size() + 1, sizeof(buf)));
                buf[sizeof(buf) - 1] = '\0';

                if (ImGui::InputText(propCtx.prettyName.c_str(), buf, sizeof(buf))) {
                    s = buf;
                    ctx.pushModified();
                }
            },
        });

    // glm::vec3 类型渲染器
    registry.registerRenderer(
        ya::type_index_v<glm::vec3>,
        TypeRenderer{
            .typeName   = "glm::vec3",
            .renderFunc = [](void *instance, const PropertyRenderContext &propCtx, RenderContext &ctx) {
                auto *ptr      = static_cast<float *>(instance);
                bool  modified = false;
                if (propCtx.bColor) {
                    modified = ImGui::ColorEdit3(propCtx.prettyName.c_str(), ptr);
                }
                else {
                    modified = ImGui::DragFloat3(propCtx.prettyName.c_str(), ptr);
                }
                if (modified) {
                    ctx.pushModified();
                }
            },
        });

    // glm::vec4 类型渲染器（颜色）
    registry.registerRenderer(
        ya::type_index_v<glm::vec4>,
        TypeRenderer{
            .typeName   = "glm::vec4",
            .renderFunc = [](void *instance, const PropertyRenderContext &propCtx, RenderContext &ctx) {
                float *val      = static_cast<float *>(instance);
                bool   modified = false;
                if (propCtx.bColor) {
                    modified = ImGui::ColorEdit4(propCtx.prettyName.c_str(), val);
                }
                else {
                    modified = ImGui::DragFloat4(propCtx.prettyName.c_str(), val);
                }
                if (modified) {
                    ctx.pushModified();
                }
            },
        });

    registry.registerRenderer(
        ya::type_index_v<ModelRef>,
        TypeRenderer{
            .typeName   = "ModelRef",
            .renderFunc = [](void *instance, const PropertyRenderContext &propCtx, RenderContext &ctx) {
                pathWrapper(instance, propCtx, ctx, [](void *instance, const PropertyRenderContext &propCtx) {
                    auto &assetRef = *static_cast<AssetRefBase *>(instance);
                    if (auto *app = App::get()) {
                        if (auto *editorLayer = app->_editorLayer) {
                            editorLayer->_filePicker.openModelPicker(
                                assetRef.getPath(),
                                [&assetRef](const std::string &newPath) {
                                    auto p = VFS::get()->relativeTo(newPath, VFS::get()->getProjectRoot()).string();
                                    assetRef.setPath(p);
                                });
                        }
                    }
                });
            },
        });

    registry.registerRenderer(
        ya::type_index_v<TextureRef>,
        TypeRenderer{
            .typeName   = "TextureRef",
            .renderFunc = [](void *instance, const PropertyRenderContext &propCtx, RenderContext &ctx) {
                pathWrapper(instance, propCtx, ctx, [](void *instance, const PropertyRenderContext &propCtx) {
                    auto &assetRef = *static_cast<AssetRefBase *>(instance);
                    if (auto *app = App::get()) {
                        if (auto *editorLayer = app->_editorLayer) {
                            editorLayer->_filePicker.openTexturePicker(
                                assetRef.getPath(),
                                [&assetRef](const std::string &newPath) {
                                    auto p = VFS::get()->relativeTo(newPath, VFS::get()->getProjectRoot()).string();
                                    assetRef.setPath(p);
                                });
                        }
                    }
                });
            },
        });
}

void pathWrapper(void *instance, const PropertyRenderContext &propCtx, RenderContext &ctx, auto internal)
{
    auto &assetRef = *static_cast<AssetRefBase *>(instance);

    // Display current path
    const auto &p           = assetRef.getPath();
    std::string displayPath = p.empty() ? "[No Path]" : p;

    // Path input field (read-only display)
    ImGui::Text("%s:", propCtx.prettyName.c_str());
    ImGui::SameLine();
    ImGui::SetNextItemWidth(-80); // Leave space for button

    char buffer[256];
    strncpy(buffer, displayPath.c_str(), sizeof(buffer) - 1);
    buffer[sizeof(buffer) - 1] = '\0';

    if (ImGui::InputText(("##" + propCtx.prettyName).c_str(), buffer, sizeof(buffer))) {
        assetRef.setPath(buffer);
        ctx.pushModified();
    }

    // Browse button - access FilePicker through App::get()->_editorLayer
    ImGui::SameLine();
    if (ImGui::Button(("Browse##" + propCtx.prettyName).c_str())) {
        internal(instance, propCtx);
    }
};

struct EditorLayer *getEditor()
{
    if (auto *app = App::get()) {
        return app->_editorLayer;
    }
    return nullptr;
}

struct FilePicker *getFilePicker()
{
    if (auto *editor = getEditor()) {
        return &editor->_filePicker;
    }
    return nullptr;
}



} // namespace ya
