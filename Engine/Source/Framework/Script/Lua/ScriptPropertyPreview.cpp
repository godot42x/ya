#include "Script/Lua/ScriptPropertyPreview.h"

#include "Core/Log.h"
#include "Core/System/VirtualFileSystem.h"
#include "Script/Lua/LuaScriptBinding.h"
#include "Script/Lua/LuaScriptingSystem.h"

namespace ya
{
namespace
{

struct ScriptPropertyPreview final : IScriptPropertyPreview
{
    sol::state _lua;
    bool       _bReady = false;

    void ensureReady()
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

        registerLuaScriptBindings(_lua);
        _bReady = true;
    }

    bool load(ScriptInstance& script) override
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
            applyScriptPropertyOverrides(script, table, _lua);
            captureScriptProperties(script, table);
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
};

} // namespace

IScriptPropertyPreview* createScriptPropertyPreview()
{
    return new ScriptPropertyPreview();
}

void destroyScriptPropertyPreview(IScriptPropertyPreview* preview)
{
    delete static_cast<ScriptPropertyPreview*>(preview);
}

} // namespace ya
