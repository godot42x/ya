// `entity:call(name, ...)` end to end: the native method on the shared export
// walks the entity's loaded scripts and the first one defining the name
// answers (rpg-prototype R2a).

#include "Core/Scripting/ScriptBindings.h"
#include "ECS/Entity.h"
#include "ECS/Systems/Components/LuaScriptComponent.h"
#include "ECS/Systems/WorldScriptRuntime.h"
#include "GameRuntime/Script/GameplayScriptFunctions.h"
#include "Script/Lua/LuaTesting.h"
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
        const std::string path = ScriptInstance::normalizeScriptPath("Test/Scripts/" + name + ".lua");
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

/// Registers `world.scriptOf` before the state is built, and drops those
/// captures after the Lua state is destroyed (member order).
struct FScriptOfWorld
{
    Scene scene{"World"};
    struct Clear
    {
        ~Clear() { registerGameplayScriptFunctions({}); }
    } clear;
    LuaScriptingSystem                 lua;
    std::map<std::string, std::string> sources;

    FScriptOfWorld()
    {
        GameplayScriptContext ctx;
        ctx.activeScene  = [this]() -> Scene* { return &scene; };
        ctx.worldRuntime = [this]() -> IWorldScriptRuntime* { return &lua; };
        registerGameplayScriptFunctions(ctx);
        lua.setRuntimeServices({
            .activeScene = [this]() { return &scene; },
            .readScript  = [this](const std::string& path, std::string& out) {
                const auto it = sources.find(path);
                if (it == sources.end()) {
                    return false;
                }
                out = it->second;
                return true;
            },
        });
        lua.init();
    }

    ~FScriptOfWorld() { lua.onStop(); }

    std::string define(const std::string& name, std::string source)
    {
        const std::string path = ScriptInstance::normalizeScriptPath("Test/Scripts/" + name + ".lua");
        sources[path]          = std::move(source);
        return path;
    }

    Entity* addScripted(const std::string& name, const std::vector<std::pair<std::string, std::string>>& scripts)
    {
        Node3D* node = scene.createNode3D(name);
        auto*   comp = node->getEntity()->addComponent<LuaScriptComponent>();
        for (const auto& [scriptName, source] : scripts) {
            comp->addScript(define(scriptName, source));
        }
        return node->getEntity();
    }
};

TEST(LuaEntityScriptCallTest, ScriptOfReturnsTheLiveSelf)
{
    FScriptOfWorld world;
    Entity* npc = world.addScripted("Npc",
                                    {{"Talk",
                                      "local S = { tag = 'talk' }\n"
                                      "function S:ping() return 'hi' end\n"
                                      "return S\n"}});
    world.lua.onUpdate(0.016f);
    world.lua.lua()["target"] = LuaScriptObject{script::entityRef(npc)};

    const auto result = world.lua.lua().safe_script(R"(
local s = world.scriptOf(target)
s.extra = 7
local again = world.scriptOf(target)
return rawequal(s, again), again.extra, again:ping(), again.entity:getName(), again.tag
)",
                                                     sol::script_pass_on_error);
    ASSERT_TRUE(result.valid()) << sol::error(result).what();
    EXPECT_TRUE(result.get<bool>(0));
    EXPECT_EQ(result.get<int>(1), 7);
    EXPECT_EQ(result.get<std::string>(2), "hi");
    EXPECT_EQ(result.get<std::string>(3), "Npc");
    EXPECT_EQ(result.get<std::string>(4), "talk");
}

TEST(LuaEntityScriptCallTest, ScriptOfWrongMethodIsAttemptToCallNil)
{
    FScriptOfWorld world;
    Entity* npc = world.addScripted("Npc",
                                    {{"Talk",
                                      "local S = {}\n"
                                      "function S:ping() return true end\n"
                                      "return S\n"}});
    world.lua.onUpdate(0.016f);
    world.lua.lua()["target"] = LuaScriptObject{script::entityRef(npc)};

    const auto result = world.lua.lua().safe_script("local s = world.scriptOf(target); return s:noSuch()",
                                                     sol::script_pass_on_error);
    ASSERT_FALSE(result.valid());
    EXPECT_NE(std::string(sol::error(result).what()).find("attempt to call"), std::string::npos);
}

TEST(LuaEntityScriptCallTest, ScriptOfWithoutAScriptIsNil)
{
    FScriptOfWorld world;
    Node3D* bare = world.scene.createNode3D("Bare");
    Entity* pending = world.addScripted("Pending",
                                        {{"Later",
                                          "local S = {}\nfunction S:ping() return true end\nreturn S\n"}});
    world.lua.lua()["bare"]    = LuaScriptObject{script::entityRef(bare->getEntity())};
    world.lua.lua()["pending"] = LuaScriptObject{script::entityRef(pending)};

    const auto result = world.lua.lua().safe_script(
        "return world.scriptOf(bare) == nil, world.scriptOf(pending) == nil", sol::script_pass_on_error);
    ASSERT_TRUE(result.valid()) << sol::error(result).what();
    EXPECT_TRUE(result.get<bool>(0));
    EXPECT_TRUE(result.get<bool>(1)) << "an attached script that has not loaded is nil";
}

TEST(LuaEntityScriptCallTest, ScriptOfPathFilter)
{
    FScriptOfWorld world;
    const std::string front = world.define("Front", "local S = { tag = 'front' }\nreturn S\n");
    const std::string back  = world.define("Back", "local S = { tag = 'back' }\nfunction S:fromBack() return true end\nreturn S\n");
    Node3D* node = world.scene.createNode3D("Npc");
    auto*   comp = node->getEntity()->addComponent<LuaScriptComponent>();
    comp->addScript(front);
    comp->addScript(back);
    world.lua.onUpdate(0.016f);
    world.lua.lua()["target"]    = LuaScriptObject{script::entityRef(node->getEntity())};
    world.lua.lua()["frontPath"] = front;
    world.lua.lua()["backPath"]  = back;

    const auto result = world.lua.lua().safe_script(R"(
local any = world.scriptOf(target)
local picked = world.scriptOf(target, backPath)
local same = world.scriptOf(target, frontPath)
local miss = world.scriptOf(target, "Test/Scripts/missing.lua")
return any.tag, picked.tag, picked:fromBack(), same.tag, miss == nil
)",
                                                     sol::script_pass_on_error);
    ASSERT_TRUE(result.valid()) << sol::error(result).what();
    EXPECT_EQ(result.get<std::string>(0), "front");
    EXPECT_EQ(result.get<std::string>(1), "back");
    EXPECT_TRUE(result.get<bool>(2));
    EXPECT_EQ(result.get<std::string>(3), "front");
    EXPECT_TRUE(result.get<bool>(4));
}

TEST(LuaEntityScriptCallTest, ScriptOfAfterHotReloadIsANewTable)
{
    FScriptOfWorld world;
    const std::string path = world.define("Gen",
                                          "local S = { generation = 1 }\n"
                                          "function S:ping() return self.generation end\n"
                                          "return S\n");
    Node3D* node = world.scene.createNode3D("Npc");
    node->getEntity()->addComponent<LuaScriptComponent>()->addScript(path);
    world.lua.onUpdate(0.016f);
    world.lua.lua()["target"] = LuaScriptObject{script::entityRef(node->getEntity())};

    const auto grabbed = world.lua.lua().safe_script("old = world.scriptOf(target); old.generation = 9; return old:ping()",
                                                      sol::script_pass_on_error);
    ASSERT_TRUE(grabbed.valid()) << sol::error(grabbed).what();
    EXPECT_EQ(grabbed.get<int>(0), 9);

    world.sources[path] = "local S = { generation = 2 }\n"
                          "function S:ping() return self.generation end\n"
                          "return S\n";
    world.lua.reloadScript(path);

    const auto result = world.lua.lua().safe_script(R"(
local now = world.scriptOf(target)
return rawequal(old, now), old.generation, now.generation, now:ping(), now.entity:getName()
)",
                                                     sol::script_pass_on_error);
    ASSERT_TRUE(result.valid()) << sol::error(result).what();
    EXPECT_FALSE(result.get<bool>(0)) << "hot reload replaces the self table";
    EXPECT_EQ(result.get<int>(1), 9) << "the table the caller already holds is unchanged";
    EXPECT_EQ(result.get<int>(2), 2);
    EXPECT_EQ(result.get<int>(3), 2);
    EXPECT_EQ(result.get<std::string>(4), "Npc");
}

} // namespace ya
