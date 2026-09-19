#include "GameEditor/UI/Ops/EditorLuaPreview.h"

#include "Core/Log.h"
#include "Core/System/VirtualFileSystem.h"

#include <glm/glm.hpp>

namespace ya
{

void EditorLuaPreview::ensureReady()
{
    if (_bReady) {
        return;
    }

    _lua.open_libraries(sol::lib::base,
                        sol::lib::package,
                        sol::lib::math,
                        sol::lib::string,
                        sol::lib::table);

    _lua["IS_EDITOR"]  = true;
    _lua["IS_RUNTIME"] = false;

    _lua.script(R"(
        package.path = package.path .. ';./Engine/Content/Lua/?.lua'
        package.path = package.path .. ';./Engine/Content/Lua/?/init.lua'
        package.path = package.path .. ';./Content/Scripts/?.lua'
        package.path = package.path .. ';./Content/Scripts/?/init.lua'
    )");

    _lua.new_usertype<glm::vec3>(
        "Vec3",
        sol::constructors<glm::vec3(), glm::vec3(float), glm::vec3(float, float, float)>(),
        "x",
        &glm::vec3::x,
        "y",
        &glm::vec3::y,
        "z",
        &glm::vec3::z);

    _bReady = true;
}

void EditorLuaPreview::releaseMatching(LuaScriptComponent& component)
{
    if (!_bReady) {
        return;
    }
    lua_State* const L = _lua.lua_state();
    if (!L) {
        return;
    }
    for (auto& script : component.scripts) {
        const bool bOurs =
            (script.self.valid() && script.self.lua_state() == L) ||
            (script.onInit.valid() && script.onInit.lua_state() == L);
        if (bOurs) {
            script.releaseLuaHandles();
        }
    }
}

bool EditorLuaPreview::load(LuaScriptComponent::ScriptInstance& script)
{
    ensureReady();
    script.bAuthoringPreviewAttempted = true;
    script.bAuthoringPreviewLoaded    = false;
    script.properties.clear();

    if (script.scriptPath.empty()) {
        return false;
    }

    VirtualFileSystem* vfs = VirtualFileSystem::get();
    std::string        scriptContent;
    if (!vfs || !vfs->readFileToString(script.scriptPath, scriptContent)) {
        YA_CORE_ERROR("[Editor Preview] Failed to read file: {}", script.scriptPath);
        return false;
    }

    try {
        sol::load_result loadResult = _lua.load(scriptContent);
        if (!loadResult.valid()) {
            sol::error err = loadResult;
            YA_CORE_ERROR("[Editor Preview] Lua syntax error in {}: {}", script.scriptPath, err.what());
            return false;
        }

        sol::protected_function_result result = loadResult();
        if (!result.valid()) {
            sol::error err = result;
            YA_CORE_ERROR("[Editor Preview] Lua execution error in {}: {}", script.scriptPath, err.what());
            return false;
        }

        if (result.get_type() != sol::type::table) {
            YA_CORE_ERROR("[Editor Preview] Script {} must return a table", script.scriptPath);
            return false;
        }

        sol::table table = result;
        script.applyPropertyOverridesTo(table, _lua);
        script.capturePropertiesFrom(table);
        script.bAuthoringPreviewLoaded = true;
        return true;
    }
    catch (const sol::error& e) {
        YA_CORE_ERROR("[Editor Preview] Exception while loading {}: {}", script.scriptPath, e.what());
    }
    catch (const std::exception& e) {
        YA_CORE_ERROR("[Editor Preview] Unexpected error: {}", e.what());
    }

    return false;
}

} // namespace ya
