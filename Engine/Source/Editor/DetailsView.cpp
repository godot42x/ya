#include "DetailsView.h"
#include "Core/Debug/Instrumentor.h"
#include "Core/System/VirtualFileSystem.h"
#include "ECS/Component/2D/UIComponent.h"
#include "ECS/Component/Material/PhongMaterialComponent.h"
#include "ECS/Component/MeshComponent.h"
#include "ECS/Component/ModelComponent.h"
#include "ReflectionCache.h"
#include "Resource/TextureLibrary.h"
#include "TypeRenderer.h"


#include "ECS/Component.h"
#include "ECS/Component/LuaScriptComponent.h"
#include "ECS/Component/Material/PhongMaterialComponent.h"
#include "ECS/Component/Material/SimpleMaterialComponent.h"
#include "ECS/Component/Material/UnlitMaterialComponent.h"
#include "ECS/Component/PointLightComponent.h"
#include "ECS/Component/RenderComponent.h"
#include "ECS/Component/TransformComponent.h"


#include "EditorLayer.h"
#include "Scene/Node.h"
#include "Scene/Scene.h"
#include <algorithm>
#include <glm/gtc/type_ptr.hpp>
#include <imgui.h>


namespace ya
{

DetailsView::DetailsView(EditorLayer* owner) : _owner(owner)
{
}

void DetailsView::onImGuiRender()
{
    YA_PROFILE_FUNCTION();
    ImGui::SetNextWindowSize(ImVec2(300, 600), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Properties"))
    {
        ImGui::End();
        return;
    }

    if (!_owner->getSelections().empty())
    {
        if (auto* firstEntity = _owner->getSelections()[0]; firstEntity->isValid()) {

            drawComponents(*firstEntity);
        }
    }

    ImGui::End();

    // 渲染文件选择器弹窗
    _filePicker.render();
}



// MARK: drawComponents
void DetailsView::drawComponents(Entity& entity)
{
    YA_PROFILE_FUNCTION();
    if (!entity)
    {
        return;
    }

    ImGui::Text("Entity ID: %u", entity.getId());
    ImGui::Separator();

    // ★ 自定义名字编辑器：优先使用 Node 的名字
    Scene* scene = entity.getScene();
    Node*  node  = scene ? scene->getNodeByEntity(&entity) : nullptr;

    ImGui::PushID("Name");
    if (node) {
        // 使用 Node 的名字
        char        buffer[256];
        std::string name = node->getName();
        strncpy_s(buffer, name.c_str(), sizeof(buffer) - 1);
        buffer[sizeof(buffer) - 1] = '\0';

        if (ImGui::InputText("Name", buffer, sizeof(buffer))) {
            node->setName(buffer);
        }
    }
    else {
        // Fallback: 使用 Entity::name（兼容 flat entity）
        char buffer[256];
        strncpy_s(buffer, entity.name.c_str(), sizeof(buffer) - 1);
        buffer[sizeof(buffer) - 1] = '\0';

        if (ImGui::InputText("Name", buffer, sizeof(buffer))) {
            entity.name = buffer;
        }
    }
    ImGui::PopID();

    drawReflectedComponent<TransformComponent>("Transform", entity, [](TransformComponent* tc) {
        // Mark both local and world as dirty, then propagate to children
        tc->markLocalDirty();
        tc->propagateWorldDirtyToChildren();
    });
    drawReflectedComponent<ModelComponent>("Model", entity, [](ModelComponent* mc) {
        mc->invalidate();
    });
    drawReflectedComponent<MeshComponent>("Mesh", entity, [](MeshComponent* mc) {
        mc->invalidate();
    });
    drawReflectedComponent<UIComponent>("UI Component", entity, [](UIComponent* uc) {});

    // 其他组件保持原有的自定义渲染逻辑
    drawComponent<SimpleMaterialComponent>("Simple Material", entity, [](SimpleMaterialComponent* smc) {
        auto* simpleMat = smc->getMaterial();
        if (!simpleMat) {
            ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.0f, 1.0f), "Material not resolved");
            return;
        }
        int colorType = static_cast<int>(simpleMat->colorType);
        if (ImGui::Combo("Color Type", &colorType, "Normal\0Texcoord\0\0")) {
            simpleMat->colorType = static_cast<SimpleMaterial::EColor>(colorType);
        }
    });

    drawReflectedComponent<RenderComponent>("Render Component", entity, [](RenderComponent* rc) {
        // TODO: implement render component details
    });

    drawComponent<UnlitMaterialComponent>("Unlit Material", entity, [](UnlitMaterialComponent* umc) {
        auto* unlitMat = umc->getMaterial();
        if (!unlitMat) {
            ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.0f, 1.0f), "Material not resolved");
            return;
        }

        if (ImGui::CollapsingHeader(unlitMat->getLabel().c_str(), ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::Indent();

            bool bDirty = false;
            bDirty |= ImGui::ColorEdit3("Base Color0", glm::value_ptr(unlitMat->uMaterial.baseColor0));
            bDirty |= ImGui::ColorEdit3("Base Color1", glm::value_ptr(unlitMat->uMaterial.baseColor1));
            bDirty |= ImGui::DragFloat("Mix Value", &unlitMat->uMaterial.mixValue, 0.01f, 0.0f, 1.0f);

            // Edit texture params (UV stored in TextureParam, not TextureView)
            auto editTextureParam = [&bDirty](const char* name, UnlitMaterial::TextureParam& param, const TextureView* tv) {
                if (!tv || !tv->texture) return;
                auto label = tv->texture->getLabel();
                if (label.empty()) label = tv->texture->getFilepath();
                ImGui::Text("%s: %s", name, label.c_str());
                std::string id = name;
                bDirty |= ImGui::Checkbox(("Enable##" + id).c_str(), &param.enable);
                // uvTransform: x,y = scale, z,w = offset
                glm::vec2 offset{param.uvTransform.z, param.uvTransform.w};
                glm::vec2 scale{param.uvTransform.x, param.uvTransform.y};
                if (ImGui::DragFloat2(("Offset##" + id).c_str(), glm::value_ptr(offset), 0.01f)) {
                    param.uvTransform.z = offset.x;
                    param.uvTransform.w = offset.y;
                    bDirty              = true;
                }
                if (ImGui::DragFloat2(("Scale##" + id).c_str(), glm::value_ptr(scale), 0.01f, 0.01f, 10.0f)) {
                    param.uvTransform.x = scale.x;
                    param.uvTransform.y = scale.y;
                    bDirty              = true;
                }
                static constexpr const auto pi = glm::pi<float>();
                bDirty |= ImGui::DragFloat(("Rotation##" + id).c_str(), &param.uvRotation, pi / 3600, -pi, pi);
            };

            editTextureParam("Texture0", unlitMat->uMaterial.textureParam0, unlitMat->getTextureView(UnlitMaterial::BaseColor0));
            editTextureParam("Texture1", unlitMat->uMaterial.textureParam1, unlitMat->getTextureView(UnlitMaterial::BaseColor1));

            if (bDirty) {
                unlitMat->setParamDirty(true);
            }
            ImGui::Unindent();
        }
    });

    drawComponent<PhongMaterialComponent>("Phong Material", entity, [](PhongMaterialComponent* pmc) {
        ya::RenderContext ctx;
        ctx.beginInstance(pmc);
        ya::renderReflectedType("PhongMaterialComponent", type_index_v<PhongMaterialComponent>, pmc, ctx, 0);

        // Unified modification handling: both sync and async modifications
        if (ctx.hasModifications()) {
            if (ctx.isModifiedPrefix("_diffuseSlot") || ctx.isModifiedPrefix("_specularSlot")) {
                pmc->invalidate();
            }
            // UV params changed -> mark param dirty
            pmc->getMaterial()->setParamDirty();
        }
        if (ImGui::Button("Invalidate")) {
            pmc->invalidate();
        }
    });

    drawReflectedComponent<PointLightComponent>("Point Light", entity, [](PointLightComponent* plc) {
        // TODO: implement point light component details
    });

    drawComponent<LuaScriptComponent>("Lua Script", entity, [this](LuaScriptComponent* lsc) {
        // Add new script button
        if (ImGui::Button("+ Add Script")) {
            _filePicker.openScriptPicker(
                "",
                [lsc](const std::string& scriptPath) {
                    lsc->addScript(scriptPath);
                });
        }

        ImGui::Separator();

        // List all scripts
        int indexToRemove = -1;
        for (size_t i = 0; i < lsc->scripts.size(); ++i) {
            auto& script = lsc->scripts[i];

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
                    _filePicker.openScriptPicker(script.scriptPath, [&script](const std::string& newPath) {
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
                        for (auto& prop : script.properties) {
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

    // Add Component button at the bottom
    drawAddComponentButton(entity);
}

void DetailsView::drawAddComponentButton(Entity& entity)
{
    ImGui::Separator();

    // Center the button
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
        // Get all registered components from ECSRegistry
        auto& ecsRegistry = ECSRegistry::get();

        // Filter input
        static char searchFilter[128] = "";
        ImGui::InputTextWithHint("##ComponentSearch", "Search...", searchFilter, sizeof(searchFilter));
        ImGui::Separator();

        std::string filterStr = searchFilter;
        std::ranges::transform(filterStr, filterStr.begin(), ::tolower);

        // static std::

        for (auto& [fname, typeIndex] : ecsRegistry.getTypeIndexCache()) {
            std::string componentName = fname.toString();

            // Apply search filter
            if (!filterStr.empty()) {
                std::string lowerName = componentName;
                std::ranges::transform(lowerName, lowerName.begin(), ::tolower);
                if (lowerName.find(filterStr) == std::string::npos) {
                    continue;
                }
            }

            auto& registry = entity.getScene()->getRegistry();
            if (ecsRegistry.hasComponent(typeIndex, registry, entity.getHandle())) {
                ImGui::BeginDisabled();
                ImGui::MenuItem(componentName.c_str());
                ImGui::EndDisabled();
            }
            else {
                if (ImGui::MenuItem(componentName.c_str())) {
                    // Create the component
                    if (void* compPtr = ecsRegistry.addComponent(typeIndex, registry, entity.getHandle())) {
                        YA_CORE_INFO("Added component '{}' to entity '{}' {}", componentName, entity.getName(), compPtr);
                    }
                    ImGui::CloseCurrentPopup();
                }
            }
        }

        ImGui::EndPopup();
    }
}

void DetailsView::renderScriptProperty(void* propPtr, void* scriptInstancePtr)
{
    using namespace ImGui;

    auto&       prop        = *static_cast<LuaScriptComponent::ScriptProperty*>(propPtr);
    auto&       script      = *static_cast<LuaScriptComponent::ScriptInstance*>(scriptInstancePtr);
    sol::table& scriptTable = script.self;

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

void DetailsView::tryLoadScriptForEditor(void* scriptPtr)
{
    using namespace ya;

    auto& script = *static_cast<LuaScriptComponent::ScriptInstance*>(scriptPtr);

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
    if (!VirtualFileSystem::get()->readFileToString(script.scriptPath, scriptContent)) {
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
    catch (const sol::error& e) {
        YA_CORE_ERROR("[Editor Preview] Exception while loading {}: {}", script.scriptPath, e.what());
        script.self = sol::lua_nil;
        script.properties.clear();
    }
    catch (const std::exception& e) {
        YA_CORE_ERROR("[Editor Preview] Unexpected error: {}", e.what());
        script.self = sol::lua_nil;
        script.properties.clear();
    }
}

void DetailsView::testNewRenderInterface(Entity& entity)
{
    // 示例：使用新的renderReflectedType接口
    if (auto* transform = entity.getComponent<TransformComponent>()) {
        ya::RenderContext ctx;
        ctx.beginInstance(transform); // Set current instance for PropertyId generation
        ya::renderReflectedType("Transform", ya::type_index_v<TransformComponent>, transform, ctx, 0);

        // 新的API用法：高效查询特定属性是否被修改
        if (ctx.isModified("position")) {
            YA_CORE_INFO("Position was modified!");
        }
        if (ctx.isModifiedPrefix("rotation")) {
            YA_CORE_INFO("Some rotation property was modified!");
        }

        // 遍历所有修改
        if (ctx.hasModifications()) {
            for (const auto& mod : ctx.modifications) {
                YA_CORE_INFO("Property {} was modified (path: {})", mod.propPath, mod.propId.id);
            }
        }
    }
}

} // namespace ya
