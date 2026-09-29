// `entity:call(name, ...)` end to end: the native method on the shared export
// walks the entity's loaded scripts and the first one defining the name
// answers (rpg-prototype R2a).

#include "Core/Scripting/ScriptBindings.h"
#include "ECS/Entity.h"
#include "ECS/Systems/Components/LuaScriptComponent.h"
#include "ECS/Systems/LuaScriptBinding.h"
#include "ECS/Systems/LuaScriptingSystem.h"
#include "Scene/Core/Scene.h"
#include "Scene/Core/SceneScriptBindings.h"
#include "Scene3D/Node3D.h"

#include <gtest/gtest.h>

#include <map>
#include <string>
#include <vector>

namespace ya
{

namespace
{

/// One Lua state over one scene, scripts served from memory, callbacks traced
/// through the global TRACE table.
struct FWorld
{
    Scene                              scene{"World"};
    LuaScriptingSystem                 lua;
    std::map<std::string, std::string> sources;

    FWorld()
    {
        lua.setRuntimeServices({
            .activeScene = [this]() { return &scene; },
            .readScript  = [this](const std::string& path, std::string& out) {
                auto it = sources.find(path);
                if (it == sources.end()) {
                    return false;
                }
                out = it->second;
                return true;
            },
        });
        lua.init();
        lua.lua()["TRACE"] = lua.lua().create_table();
    }

    ~FWorld() { lua.onStop(); }

    std::string define(const std::string& name, std::string source)
    {
        const std::string path = LuaScriptInstance::normalizeScriptPath("Test/Scripts/" + name + ".lua");
        sources[path]          = std::move(source);
        return path;
    }

    /// One entity running the given scripts in order; loading happens on the
    /// next onUpdate.
    Entity* addScripted(const std::string& name, const std::vector<std::pair<std::string, std::string>>& scripts)
    {
        Node3D* node = scene.createNode3D(name);
        auto*   comp = node->getEntity()->addComponent<LuaScriptComponent>();
        for (const auto& [scriptName, source] : scripts) {
            comp->addScript(define(scriptName, source));
        }
        return node->getEntity();
    }

    std::vector<std::string> takeTrace()
    {
        std::vector<std::string> out;
        sol::table               trace = lua.lua()["TRACE"];
        for (size_t i = 1; i <= trace.size(); ++i) {
            out.push_back(trace.get<std::string>(i));
        }
        lua.lua()["TRACE"] = lua.lua().create_table();
        return out;
    }
};

} // namespace

TEST(LuaEntityScriptCallTest, FirstScriptDefiningTheNameAnswers)
{
    FWorld world;
    Entity* npc = world.addScripted("Npc",
                                    {{"Front", "local S = {}\nfunction S:onUpdate() end\nreturn S\n"},
                                     {"Back",
                                      "local S = {}\n"
                                      "function S:greet(word)\n"
                                      "  table.insert(TRACE, 'greet:' .. word .. ':' .. self.entity:getName())\n"
                                      "  return 'hello ' .. word\n"
                                      "end\n"
                                      "return S\n"}});
    world.lua.onUpdate(0.016f);

    world.lua.lua()["target"] = LuaScriptObject{script::entityRef(npc)};
    const auto result         = world.lua.lua().safe_script("return target:call('greet', 'hi')", sol::script_pass_on_error);
    ASSERT_TRUE(result.valid()) << sol::error(result).what();
    EXPECT_EQ(result.get<std::string>(0), "hello hi");
    EXPECT_EQ(world.takeTrace(), (std::vector<std::string>{"greet:hi:Npc"}));
}

TEST(LuaEntityScriptCallTest, MissingFunctionIsNilAndArgsCross)
{
    FWorld world;
    Entity* npc = world.addScripted("Npc",
                                    {{"Talk",
                                      "local S = {}\n"
                                      "function S:sum(a, b) return a + b end\n"
                                      "return S\n"}});
    world.lua.onUpdate(0.016f);

    world.lua.lua()["target"] = LuaScriptObject{script::entityRef(npc)};
    const auto summed         = world.lua.lua().safe_script("return target:call('sum', 20, 22)", sol::script_pass_on_error);
    ASSERT_TRUE(summed.valid()) << sol::error(summed).what();
    EXPECT_EQ(summed.get<int>(0), 42);

    const auto missing = world.lua.lua().safe_script("return target:call('onInteract')", sol::script_pass_on_error);
    ASSERT_TRUE(missing.valid()) << sol::error(missing).what();
    EXPECT_EQ(missing.get<sol::object>(0).get_type(), sol::type::lua_nil);
}

TEST(LuaEntityScriptCallTest, TargetErrorRaisesAtTheCaller)
{
    FWorld world;
    Entity* npc = world.addScripted("Npc",
                                    {{"Broken",
                                      "local S = {}\n"
                                      "function S:boom() error('kaboom') end\n"
                                      "return S\n"}});
    world.lua.onUpdate(0.016f);

    world.lua.lua()["target"] = LuaScriptObject{script::entityRef(npc)};
    // A failing target is not a nil answer: pcall sees the message, so the
    // calling script can tell a bug from a quiet target.
    const auto result = world.lua.lua().safe_script(
        "local ok, err = pcall(function() return target:call('boom') end); return ok, err", sol::script_pass_on_error);
    ASSERT_TRUE(result.valid()) << sol::error(result).what();
    EXPECT_FALSE(result.get<bool>(0));
    EXPECT_NE(std::string(result.get<std::string>(1)).find("kaboom"), std::string::npos);
}

} // namespace ya
