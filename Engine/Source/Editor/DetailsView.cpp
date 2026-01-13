#include "DetailsView.h"
#include "Core/System/FileSystem.h"
#include "Editor/ContainerPropertyRenderer.h"

#include "ECS/Component.h"
#include "ECS/Component/LuaScriptComponent.h"
#include "ECS/Component/Material/LitMaterialComponent.h"
#include "ECS/Component/Material/SimpleMaterialComponent.h"
#include "ECS/Component/Material/UnlitMaterialComponent.h"
#include "ECS/Component/PointLightComponent.h"
#include "ECS/Component/TransformComponent.h"
#include "EditorLayer.h"
#include "Scene/Scene.h"
#include <algorithm>
#include <glm/gtc/type_ptr.hpp>
#include <imgui.h>

#include "reflects-core/lib.h"

namespace ya
{

// 获取或创建反射缓存
ReflectionCache *DetailsView::getOrCreateReflectionCache(uint32_t typeIndex)
{
    auto it = _reflectionCache.find(typeIndex);

    if (it != _reflectionCache.end()) {
        if (auto &cache = it->second; cache.isValid(typeIndex)) {
            // TODO: check type properties changed
            return &it->second;
        }
    }

    auto            cls = ClassRegistry::instance().getClass(typeIndex);
    ReflectionCache cache;
    cache.componentClassPtr = cls;
    cache.propertyCount     = cls ? cls->properties.size() : 0;
    cache.typeIndex         = typeIndex;

    return &(_reflectionCache[typeIndex] = cache);
}


// 递归反射属性渲染
bool DetailsView::renderReflectedType(const std::string &name, uint32_t typeIndex, void *instancePtr, int depth)
{
    if (depth >= MAX_RECURSION_DEPTH) {
        ImGui::TextDisabled("%s: [max recursion depth reached]", name.c_str());
        return false;
    }

    if (auto it = _typeRender.find(typeIndex); it != _typeRender.end()) {
        return it->second.func(instancePtr,
                               PropRenderContext{
                                   .name = name,
                               });
    }

    bool bModified = false;

    auto cache = getOrCreateReflectionCache(typeIndex);
    if (cache && cache->componentClassPtr && cache->propertyCount > 0) {
        ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_FramePadding;

        auto iterateChildren = [&]() {
            // 优化：预先缓存所有属性信息，避免每次查找
            if (cache->propertyCache.empty() && cache->propertyCount > 0) {
                for (auto &[propName, prop] : cache->componentClassPtr->properties) {
                    auto &propCache       = cache->propertyCache[propName];
                    propCache.isContainer = ::ya::reflection::PropertyContainerHelper::isContainer(prop);
                    if (propCache.isContainer) {
                        propCache.containerAccessor = ::ya::reflection::PropertyContainerHelper::getContainerAccessor(prop);
                    }
                }
            }

            for (auto &[propName, prop] : cache->componentClassPtr->properties) {
                auto subPropInstancePtr = prop.addressGetterMutable(instancePtr);

                // 从缓存获取容器信息（已预先填充）
                const auto &propCache   = cache->propertyCache[propName];
                const bool  isContainer = propCache.isContainer;

                if (isContainer) {
                    // 容器属性：只显示简单信息，不渲染内容
                    // 注意：不调用 getSize() 避免虚函数开销
                    // ImGui::TextDisabled("%s: [Container]", propName.c_str());

                    // 如果需要启用完整容器渲染，取消下面的注释
                    bool containerModified = ya::editor::ContainerPropertyRenderer::renderContainer(
                        propName,
                        prop,
                        subPropInstancePtr,
                        [this, depth](const std::string &label, void *elementPtr, uint32_t elementTypeIndex) -> bool {
                            return renderReflectedType(label, elementTypeIndex, elementPtr, depth + 2);
                        });
                    if (containerModified) {
                        bModified = true;
                    }
                }
                else {
                    // 普通属性
                    if (renderReflectedType(propName, prop.typeIndex, subPropInstancePtr, depth + 1)) {
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
            // 只有在TreeNode展开时才执行iterateChildren，避免不必要的计算
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


DetailsView::DetailsView(EditorLayer *owner) : _owner(owner)
{
    _typeRender.insert(
        {
            ya::type_index_v<int>,
            PropRender{
                .typeName = "int",
                .func     = [](void *instance, const PropRenderContext &ctx) {
                    return ImGui::InputInt(ctx.name.c_str(), static_cast<int *>(instance));
                },
            },
        });
    _typeRender.insert(
        {
            ya::type_index_v<float>,
            PropRender{
                .typeName = "float",
                .func     = [](void *instance, const PropRenderContext &ctx) {
                    return ImGui::InputFloat(ctx.name.c_str(), static_cast<float *>(instance));
                },
            },
        });

    _typeRender.insert(
        {
            ya::type_index_v<std::string>,
            PropRender{
                .typeName = "std::string",
                .func     = [](void *instance, const PropRenderContext &ctx) {
                    auto &s = *static_cast<std::string *>(instance);
                    char  buf[256];
                    std::memcpy(buf, s.c_str(), std::min(s.size() + 1, sizeof(buf)));
                    buf[sizeof(buf) - 1] = '\0';

                    bool bModified = ImGui::InputText(ctx.name.c_str(), buf, sizeof(buf));
                    if (bModified) {
                        s = buf;
                    }
                    return bModified;
                },
            },
        });
    _typeRender.insert(
        {
            ya::type_index_v<glm::vec3>,
            PropRender{
                .typeName = "glm::vec3",
                .func =
                    [](void *instance, const PropRenderContext &ctx) {
                        bool bModified = ImGui::DragFloat3(ctx.name.c_str(), (float *)instance, 0.1f);
                        return bModified;
                    },
            },
        });
    _typeRender.insert(
        {
            ya::type_index_v<glm::vec4>,
            {
                .typeName = "glm::vec4",
                .func =
                    [](void *instance, const PropRenderContext &ctx) {
                        bool bModified = ImGui::ColorEdit4(ctx.name.c_str(), (float *)instance);
                        return bModified;
                    },
            },
        });
};


void DetailsView::onImGuiRender()
{
    ImGui::SetNextWindowSize(ImVec2(300, 600), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Properties"))
    {
        ImGui::End();
        return;
    }

    if (!_owner->getSelections().empty())
    {
        if (auto *firstEntity = _owner->getSelections()[0]; firstEntity->isValid()) {

            drawComponents(*firstEntity);
        }
    }

    ImGui::End();

    // 渲染文件选择器弹窗
    _filePicker.render();
}



void DetailsView::drawComponents(Entity &entity)
{
    if (!entity)
    {
        return;
    }

    ImGui::Text("Entity ID: %u", entity.getId());
    ImGui::Separator();

    drawReflectedComponent<TagComponent>("Tag", entity, nullptr);
    drawReflectedComponent<TransformComponent>("Transform", entity, [](TransformComponent *tc) {
        tc->bDirty = true;
    });

    // 其他组件保持原有的自定义渲染逻辑
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
            if (ImGui::CollapsingHeader(mat->getLabel().c_str(), ImGuiTreeNodeFlags_DefaultOpen)) {
                ImGui::Indent();

                auto unlitMat = mat->as<UnlitMaterial>();
                bool bDirty   = false;
                bDirty |= ImGui::ColorEdit3("Base Color0", glm::value_ptr(unlitMat->uMaterial.baseColor0));
                bDirty |= ImGui::ColorEdit3("Base Color1", glm::value_ptr(unlitMat->uMaterial.baseColor1));
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
        }
    });

    drawComponent<LitMaterialComponent>("Lit Material", entity, [](LitMaterialComponent *lmc) {
        for (auto [mat, meshIndex] : lmc->getMaterial2MeshIDs()) {
            if (ImGui::CollapsingHeader(mat->getLabel().c_str(), ImGuiTreeNodeFlags_DefaultOpen)) {
                ImGui::Indent();
                ImGui::PushID(mat->getLabel().c_str());

                auto litMat = mat->as<LitMaterial>();
                bool bDirty = false;
                // bDirty |= ImGui::ColorEdit3("Object Color", glm::value_ptr(litMat->uParams.objectColor));
                // bDirty |= ImGui::DragFloat3("Base Color", glm::value_ptr(litMat->uMaterial.baseColor), 0.1f);
                bDirty |= ImGui::ColorEdit3("Ambient", glm::value_ptr(litMat->uParams.ambient));
                bDirty |= ImGui::ColorEdit3("Diffuse", glm::value_ptr(litMat->uParams.diffuse));
                bDirty |= ImGui::ColorEdit3("Specular", glm::value_ptr(litMat->uParams.specular));
                bDirty |= ImGui::SliderFloat("Shininess", &litMat->uParams.shininess, 0.0f, 256.f);
                // for (uint32_t i = 0; i < litMat->_textureViews.size(); i++) {
                //     auto &tv    = litMat->_textureViews[i];
                //     auto  label = tv.texture->getLabel();
                //     if (label.empty()) {
                //         label = tv.texture->getFilepath();
                //     }
                //     ImGui::Text("Texture %d: %s", i, label.c_str());
                //     bDirty |= ImGui::Checkbox(std::format("Enable##{}", i).c_str(), &tv.bEnable);
                //     bDirty |= ImGui::DragFloat2(std::format("Offset##{}", i).c_str(),
                // glm::value_ptr(tv.uvTranslation), 0.01f);
                //     bDirty |= ImGui::DragFloat2(std::format("Scale##{}", i).c_str(),
                // glm::value_ptr(tv.uvScale), 0.01f, 0.01f, 10.0f);
                // }
                if (bDirty) {
                    litMat->setParamDirty(true);
                }

                ImGui::PopID();
                ImGui::Unindent();
            }
        }
    });

    drawComponent<PointLightComponent>("Point Light", entity, [](PointLightComponent *plc) {
        bool bDirty = false;
        bDirty |= ImGui::ColorEdit3("Color", glm::value_ptr(plc->_color));
        bDirty |= ImGui::DragFloat("Intensity", &plc->_intensity, 0.1f, 0.0f, 100.0f);
        bDirty |= ImGui::DragFloat("Radius", &plc->_range, 0.1f, 0.0f, 100.0f);
        // plc.
    });

    drawComponent<LuaScriptComponent>("Lua Script", entity, [this](LuaScriptComponent *lsc) {
        // Add new script button
        if (ImGui::Button("+ Add Script")) {
            _filePicker.openScriptPicker(
                "",
                [lsc](const std::string &scriptPath) {
                    lsc->addScript(scriptPath);
                });
        }

        ImGui::Separator();

        // List all scripts
        int indexToRemove = -1;
        for (size_t i = 0; i < lsc->scripts.size(); ++i) {
            auto &script = lsc->scripts[i];

            ImGui::PushID(i);

            // Script header with enable/disable checkbox
            bool headerOpen = ImGui::CollapsingHeader(
                script.scriptPath.empty() ? "[Empty Script]" : script.scriptPath.c_str(),
                ImGuiTreeNodeFlags_DefaultOpen);

            ImGui::Checkbox("Enabled##enabled", &script.enabled);

            if (headerOpen) {
                ImGui::Indent();

                // Script path input
                char buffer[DETAILS_SCRIPT_INPUT_BUFFER_SIZE];
                memset(buffer, 0, sizeof(buffer));
                strncpy(buffer, script.scriptPath.c_str(), sizeof(buffer) - 1);

                ImGui::SetNextItemWidth(-80); // 留出按钮空间
                if (ImGui::InputText("##ScriptPath", buffer, sizeof(buffer))) {
                    script.scriptPath              = std::string(buffer);
                    script.bLoaded                 = false; // Force reload
                    script.bEditorPreviewAttempted = false; // 重置编辑器预览标记
                }

                ImGui::SameLine();
                if (ImGui::Button("Browse...")) {
                    _filePicker.openScriptPicker(script.scriptPath, [&script](const std::string &newPath) {
                        script.scriptPath              = newPath;
                        script.bLoaded                 = false;
                        script.bEditorPreviewAttempted = false;
                    });
                }

                // Status and auto-preview
                bool hasValidPath  = !script.scriptPath.empty();
                bool hasProperties = script.self.valid() && !script.properties.empty();

                if (script.bLoaded) {
                    ImGui::TextColored(ImVec4(0, 1, 0, 1), "Status: Loaded (Runtime)");
                }
                else if (hasProperties) {
                    ImGui::TextColored(ImVec4(0.3f, 0.7f, 1.0f, 1), "Status: Preview Mode (Editor)");
                }
                else {
                    ImGui::TextColored(ImVec4(1, 0.5f, 0, 1), "Status: Not Loaded");
                }

                // Auto-preview on first expand (if not runtime-loaded)
                if (hasValidPath && !script.bLoaded && !script.bEditorPreviewAttempted) {
                    tryLoadScriptForEditor(&script);
                }

                // 显示脚本属性
                if (script.self.valid()) {
                    ImGui::Separator();

                    // 刷新属性列表（首次或需要时）
                    if (script.properties.empty()) {
                        script.refreshProperties();
                    }

                    if (!script.properties.empty()) {
                        ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "Script Properties:");

                        // 渲染每个属性
                        for (auto &prop : script.properties) {
                            renderScriptProperty(&prop, &script);
                        }

                        // 手动刷新按钮
                        if (ImGui::Button("Refresh Properties")) {
                            script.refreshProperties();
                        }
                    }
                    else {
                        ImGui::TextDisabled("No properties found");
                        ImGui::TextDisabled("Tip: Use _PROPERTIES table to define editable properties");
                    }
                }
                else if (hasValidPath) {
                    // 显示加载失败信息
                    ImGui::Separator();
                    ImGui::TextColored(ImVec4(1, 0, 0, 1), "Failed to load script");
                    ImGui::TextDisabled("Check console for error details");
                    if (ImGui::Button("Retry Load")) {
                        script.bEditorPreviewAttempted = false; // 重置标记
                        tryLoadScriptForEditor(&script);
                    }
                }

                ImGui::Separator();

                // Remove button
                if (ImGui::Button("Remove Script")) {
                    indexToRemove = i;
                }

                ImGui::Unindent();
            }

            ImGui::PopID();
            ImGui::Separator();
        }

        // Remove marked script
        if (indexToRemove >= 0) {
            lsc->scripts.erase(lsc->scripts.begin() + indexToRemove);
        }
    });
}

void DetailsView::renderScriptProperty(void *propPtr, void *scriptInstancePtr)
{
    using namespace ImGui;

    auto       &prop        = *static_cast<LuaScriptComponent::ScriptProperty *>(propPtr);
    auto       &script      = *static_cast<LuaScriptComponent::ScriptInstance *>(scriptInstancePtr);
    sol::table &scriptTable = script.self;

    // 显示 tooltip（如果有）
    if (!prop.tooltip.empty()) {
        TextDisabled("(?)");
        if (IsItemHovered()) {
            SetTooltip("%s", prop.tooltip.c_str());
        }
        SameLine();
    }

    bool     bModified = false;
    std::any anyValue; // 直接存储类型化的值

    if (prop.typeHint == "float")
    {
        float value = prop.value.as<float>();
        if (DragFloat(prop.name.c_str(), &value, 0.1f, prop.min, prop.max))
        {
            scriptTable[prop.name] = value;
            prop.value             = sol::make_object(scriptTable.lua_state(), value);
            anyValue               = value;
            bModified              = true;
        }
    }
    else if (prop.typeHint == "int")
    {
        int value = static_cast<int>(prop.value.as<float>());
        if (DragInt(prop.name.c_str(), &value, 1.0f, (int)prop.min, (int)prop.max))
        {
            scriptTable[prop.name] = value;
            prop.value             = sol::make_object(scriptTable.lua_state(), value);
            anyValue               = value;
            bModified              = true;
        }
    }
    else if (prop.typeHint == "bool")
    {
        bool value = prop.value.as<bool>();
        if (Checkbox(prop.name.c_str(), &value))
        {
            scriptTable[prop.name] = value;
            prop.value             = sol::make_object(scriptTable.lua_state(), value);
            anyValue               = value;
            bModified              = true;
        }
    }
    else if (prop.typeHint == "string")
    {
        std::string value = prop.value.as<std::string>();
        char        buffer[DETAILS_SCRIPT_INPUT_BUFFER_SIZE];
        memset(buffer, 0, sizeof(buffer));
        strncpy(buffer, value.c_str(), sizeof(buffer) - 1);
        if (InputText(prop.name.c_str(), buffer, sizeof(buffer)))
        {
            std::string newValue   = buffer;
            scriptTable[prop.name] = newValue;
            prop.value             = sol::make_object(scriptTable.lua_state(), newValue);
            anyValue               = newValue;
            bModified              = true;
        }
    }
    else if (prop.typeHint == "Vec3")
    {
        glm::vec3 value = prop.value.as<glm::vec3>();
        if (DragFloat3(prop.name.c_str(), &value.x, 0.1f))
        {
            scriptTable[prop.name] = value;
            prop.value             = sol::make_object(scriptTable.lua_state(), value);
            anyValue               = value;
            bModified              = true;
        }
    }
    else
    {
        // 未知类型，只显示不可编辑
        TextDisabled("%s: [%s]", prop.name.c_str(), prop.typeHint.c_str());
    }

    // 如果属性被修改，直接保存 std::any 到 propertyOverrides
    if (bModified)
    {
        script.propertyOverrides[prop.name] = anyValue;
        YA_CORE_TRACE("[Editor] Property '{}' modified (type: {})", prop.name, anyValue.type().name());
    }
}

void DetailsView::tryLoadScriptForEditor(void *scriptPtr)
{
    using namespace ya;

    auto &script = *static_cast<LuaScriptComponent::ScriptInstance *>(scriptPtr);

    // 初始化编辑器 Lua 状态（仅一次）
    if (!_editorLuaInitialized) {
        YA_CORE_INFO("Initializing editor Lua state for property preview...");

        _editorLua.open_libraries(sol::lib::base,
                                  sol::lib::package,
                                  sol::lib::math,
                                  sol::lib::string,
                                  sol::lib::table);

        // 设置全局环境标识
        _editorLua["IS_EDITOR"]  = true;
        _editorLua["IS_RUNTIME"] = false;

        // 配置 Lua 模块搜索路径（与运行时保持一致）
        _editorLua.script(R"(
            package.path = package.path .. ';./Engine/Content/Lua/?.lua'
            package.path = package.path .. ';./Engine/Content/Lua/?/init.lua'
            package.path = package.path .. ';./Content/Scripts/?.lua'
            package.path = package.path .. ';./Content/Scripts/?/init.lua'
            print('[Editor Lua] Package paths: ' .. package.path)
        )");

        // 注册必要的类型
        _editorLua.new_usertype<glm::vec3>("Vec3",
                                           sol::constructors<glm::vec3(), glm::vec3(float), glm::vec3(float, float, float)>(),
                                           "x",
                                           &glm::vec3::x,
                                           "y",
                                           &glm::vec3::y,
                                           "z",
                                           &glm::vec3::z,
                                           "new",
                                           sol::factories(
                                               []() { return glm::vec3(0.0f); },
                                               [](float x, float y, float z) { return glm::vec3(x, y, z); }));

        _editorLuaInitialized = true;
        YA_CORE_INFO("Editor Lua state initialized");
    }

    // 加载脚本文件
    std::string scriptContent;
    if (!FileSystem::get()->readFileToString(script.scriptPath, scriptContent)) {
        YA_CORE_ERROR("[Editor Preview] Failed to read file: {}", script.scriptPath);
        // 文件读取失败时清空旧数据，不再重试（直到路径被修改）
        script.bEditorPreviewAttempted = true;
        script.self                    = sol::lua_nil;
        script.properties.clear();
        return;
    }

    YA_CORE_INFO("[Editor Preview] Loading script: {}", script.scriptPath);

    // 标记已尝试加载（防止重复）
    script.bEditorPreviewAttempted = true;

    try {
        // 在编辑器 Lua 状态中执行
        sol::load_result loadResult = _editorLua.load(scriptContent);
        if (!loadResult.valid()) {
            sol::error err = loadResult;
            YA_CORE_ERROR("[Editor Preview] Lua syntax error in {}: {}", script.scriptPath, err.what());
            script.self = sol::lua_nil;
            script.properties.clear();
            return;
        }

        sol::protected_function_result result = loadResult();
        if (!result.valid()) {
            sol::error err = result;
            YA_CORE_ERROR("[Editor Preview] Lua execution error in {}: {}", script.scriptPath, err.what());
            script.self = sol::lua_nil;
            script.properties.clear();
            return;
        }

        if (result.get_type() != sol::type::table) {
            YA_CORE_ERROR("[Editor Preview] Script {} must return a table", script.scriptPath);
            script.self = sol::lua_nil;
            script.properties.clear();
            return;
        }

        sol::table scriptTable = result;
        script.self            = scriptTable;

        // 刷新属性（不调用 onInit）
        script.refreshProperties();

        YA_CORE_INFO("[Editor Preview] Successfully loaded script: {} ({} properties)",
                     script.scriptPath,
                     script.properties.size());
    }
    catch (const sol::error &e) {
        YA_CORE_ERROR("[Editor Preview] Exception while loading {}: {}", script.scriptPath, e.what());
        script.self = sol::lua_nil;
        script.properties.clear();
    }
    catch (const std::exception &e) {
        YA_CORE_ERROR("[Editor Preview] Unexpected error: {}", e.what());
        script.self = sol::lua_nil;
        script.properties.clear();
    }
}

} // namespace ya
