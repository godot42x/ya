#include "Editor/Inspector/DetailsViewInternal.h"
#include "ECS/Component/LuaScriptComponent.h"
#include "Core/System/VirtualFileSystem.h"

namespace ya
{

void DetailsView::renderScriptProperty(void* propPtr, void* scriptInstancePtr)
{
    using namespace ImGui;

    auto&       prop        = *static_cast<LuaScriptComponent::ScriptProperty*>(propPtr);
    auto&       script      = *static_cast<LuaScriptComponent::ScriptInstance*>(scriptInstancePtr);
    sol::table& scriptTable = script.self;

    if (!prop.tooltip.empty()) {
        TextDisabled("(?)");
        if (IsItemHovered()) {
            SetTooltip("%s", prop.tooltip.c_str());
        }
        SameLine();
    }

    bool     bModified = false;
    std::any anyValue;

    if (prop.typeHint == "float") {
        float value = prop.value.as<float>();
        if (DragFloat(prop.name.c_str(), &value, 0.1f, prop.min, prop.max)) {
            scriptTable[prop.name] = value;
            prop.value             = sol::make_object(scriptTable.lua_state(), value);
            anyValue               = value;
            bModified              = true;
        }
    }
    else if (prop.typeHint == "int") {
        int value = static_cast<int>(prop.value.as<float>());
        if (DragInt(prop.name.c_str(), &value, 1.0f, (int)prop.min, (int)prop.max)) {
            scriptTable[prop.name] = value;
            prop.value             = sol::make_object(scriptTable.lua_state(), value);
            anyValue               = value;
            bModified              = true;
        }
    }
    else if (prop.typeHint == "bool") {
        bool value = prop.value.as<bool>();
        if (Checkbox(prop.name.c_str(), &value)) {
            scriptTable[prop.name] = value;
            prop.value             = sol::make_object(scriptTable.lua_state(), value);
            anyValue               = value;
            bModified              = true;
        }
    }
    else if (prop.typeHint == "string") {
        std::string value = prop.value.as<std::string>();
        char        buffer[DETAILS_SCRIPT_INPUT_BUFFER_SIZE];
        strncpy_s(buffer, value.c_str(), _TRUNCATE);
        if (InputText(prop.name.c_str(), buffer, sizeof(buffer))) {
            std::string newValue   = buffer;
            scriptTable[prop.name] = newValue;
            prop.value             = sol::make_object(scriptTable.lua_state(), newValue);
            anyValue               = newValue;
            bModified              = true;
        }
    }
    else if (prop.typeHint == "Vec3") {
        glm::vec3 value = prop.value.as<glm::vec3>();
        if (DragFloat3(prop.name.c_str(), &value.x, 0.1f)) {
            scriptTable[prop.name] = value;
            prop.value             = sol::make_object(scriptTable.lua_state(), value);
            anyValue               = value;
            bModified              = true;
        }
    }
    else {
        TextDisabled("%s: [%s]", prop.name.c_str(), prop.typeHint.c_str());
    }

    if (bModified) {
        script.propertyOverrides[prop.name] = anyValue;
        YA_CORE_TRACE("[Editor] Property '{}' modified (type: {})", prop.name, anyValue.type().name());
    }
}

void DetailsView::tryLoadScriptForEditor(void* scriptPtr)
{
    using namespace ya;

    auto& script = *static_cast<LuaScriptComponent::ScriptInstance*>(scriptPtr);

    if (!_editorLuaInitialized) {
        YA_CORE_INFO("Initializing editor Lua state for property preview...");

        _editorLua.open_libraries(sol::lib::base,
                                  sol::lib::package,
                                  sol::lib::math,
                                  sol::lib::string,
                                  sol::lib::table);

        _editorLua["IS_EDITOR"]  = true;
        _editorLua["IS_RUNTIME"] = false;

        _editorLua.script(R"(
            package.path = package.path .. ';./Engine/Content/Lua/?.lua'
            package.path = package.path .. ';./Engine/Content/Lua/?/init.lua'
            package.path = package.path .. ';./Content/Scripts/?.lua'
            package.path = package.path .. ';./Content/Scripts/?/init.lua'
            print('[Editor Lua] Package paths: ' .. package.path)
        )");

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

    std::string scriptContent;
    if (!VirtualFileSystem::get()->readFileToString(script.scriptPath, scriptContent)) {
        YA_CORE_ERROR("[Editor Preview] Failed to read file: {}", script.scriptPath);
        script.bAuthoringPreviewAttempted = true;
        script.self                       = sol::lua_nil;
        script.properties.clear();
        return;
    }

    YA_CORE_INFO("[Editor Preview] Loading script: {}", script.scriptPath);
    script.bAuthoringPreviewAttempted = true;

    try {
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

} // namespace ya
