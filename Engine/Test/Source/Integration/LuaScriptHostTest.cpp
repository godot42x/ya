// LuaScriptingSystem instances on hosts that are not entities (game-ui-script-framework S1).

#include "ECS/Systems/LuaScriptingSystem.h"

#include <gtest/gtest.h>

#include <deque>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace ya
{

namespace
{

/// A host that is not an entity: the instance lives in the test, the host
/// only points at it and names itself on `self`.
struct FTestHost final : ILuaScriptHost
{
    LuaScriptInstance* instance;
    std::string        name;
    const bool*        bAlive;

    FTestHost(LuaScriptInstance* inInstance, std::string inName, const bool* inAlive)
        : instance(inInstance), name(std::move(inName)), bAlive(inAlive) {}

    LuaScriptInstance* resolve(uint64_t instanceId) override
    {
        return (!bAlive || *bAlive) && instance->runtimeId == instanceId ? instance : nullptr;
    }

    void bindSelf(sol::table& self) override { self["hostName"] = name; }
};

struct FRuntime
{
    LuaScriptingSystem                 runtime;
    std::map<std::string, std::string> sources;
    /// After `runtime`: instances hold refs into its state, so they die first.
    std::deque<LuaScriptInstance>      instances;

    FRuntime()
    {
        // No init(): only the state and the instance API, no engine bindings or scene.
        runtime.lua().open_libraries(sol::lib::base, sol::lib::table, sol::lib::string);
        runtime.setRuntimeServices({
            .readScript = [this](const std::string& path, std::string& out) {
                auto it = sources.find(path);
                if (it == sources.end()) {
                    return false;
                }
                out = it->second;
                return true;
            },
        });
        runtime.lua()["TRACE"] = runtime.lua().create_table();
    }

    ~FRuntime() { runtime.destroyAll(); }

    LuaScriptInstance& make(const std::string& path) { return instances.emplace_back(LuaScriptInstance{.scriptPath = path}); }

    std::string define(const std::string& name, std::string source)
    {
        const std::string path = LuaScriptInstance::normalizeScriptPath("Test/Scripts/" + name + ".lua");
        sources[path]          = std::move(source);
        return path;
    }

    bool load(LuaScriptInstance& instance, const std::string& hostName, const bool* bAlive = nullptr)
    {
        return runtime.load(instance, std::make_unique<FTestHost>(&instance, hostName, bAlive));
    }

    std::vector<std::string> takeTrace()
    {
        std::vector<std::string> out;
        sol::table trace = runtime.lua()["TRACE"];
        for (size_t i = 1; i <= trace.size(); ++i) {
            out.push_back(trace.get<std::string>(i));
        }
        runtime.lua()["TRACE"] = runtime.lua().create_table();
        return out;
    }
};

using Trace = std::vector<std::string>;

} // namespace

TEST(LuaScriptHostTest, SameScriptOnTwoHostsHasIndependentSelf)
{
    FRuntime rt;
    const std::string path = rt.define("Counter",
                                       "local S = {}\n"
                                       "function S:onInit() self.count = 0 end\n"
                                       "function S:onUpdate(dt)\n"
                                       "  self.count = self.count + 1\n"
                                       "  table.insert(TRACE, self.hostName .. ':' .. self.count)\n"
                                       "end\n"
                                       "return S\n");
    LuaScriptInstance& a = rt.make(path);
    LuaScriptInstance& b = rt.make(path);
    ASSERT_TRUE(rt.load(a, "A"));
    ASSERT_TRUE(rt.load(b, "B"));
    EXPECT_NE(a.runtimeId, b.runtimeId);
    rt.runtime.call(a, ELuaScriptCallback::Init);
    rt.runtime.call(b, ELuaScriptCallback::Init);

    rt.runtime.call(a, ELuaScriptCallback::Update, 0.016f);
    rt.runtime.call(a, ELuaScriptCallback::Update, 0.016f);
    rt.runtime.call(b, ELuaScriptCallback::Update, 0.016f);
    EXPECT_EQ(rt.takeTrace(), (Trace{"A:1", "A:2", "B:1"}));
    EXPECT_EQ(a.self.get<int>("count"), 2);
    EXPECT_EQ(b.self.get<int>("count"), 1);
}

TEST(LuaScriptHostTest, HotReloadReachesNonEntityHosts)
{
    FRuntime rt;
    const std::string path = rt.define("Widget",
                                       "local S = { speed = 1, _PROPERTIES = { speed = { value = 1, type = 'int' } } }\n"
                                       "function S:onInit() table.insert(TRACE, 'init1:' .. self.hostName) end\n"
                                       "function S:onDestroy() table.insert(TRACE, 'destroy1') end\n"
                                       "return S\n");
    LuaScriptInstance& instance = rt.make(path);
    ASSERT_TRUE(rt.load(instance, "Panel"));
    rt.runtime.call(instance, ELuaScriptCallback::Init);
    instance.self["speed"] = 7;
    (void)rt.takeTrace();

    rt.define("Widget",
              "local S = { speed = 1, _PROPERTIES = { speed = { value = 1, type = 'int' } } }\n"
              "function S:onInit() table.insert(TRACE, 'init2:' .. self.hostName) end\n"
              "function S:onStart() table.insert(TRACE, 'start2') end\n"
              "return S\n");
    rt.runtime.reloadScript(path);

    EXPECT_EQ(rt.takeTrace(), (Trace{"destroy1", "init2:Panel", "start2"}));
    EXPECT_TRUE(instance.bLoaded);
    EXPECT_EQ(instance.self.get<int>("speed"), 7);
}

TEST(LuaScriptHostTest, CallbackErrorDoesNotStopOtherInstances)
{
    FRuntime rt;
    const std::string bad  = rt.define("Bad",
                                      "local S = {}\n"
                                      "function S:onUpdate(dt) table.insert(TRACE, 'bad'); error('boom') end\n"
                                      "return S\n");
    const std::string good = rt.define("Good",
                                       "local S = {}\n"
                                       "function S:onUpdate(dt) table.insert(TRACE, 'good') end\n"
                                       "return S\n");
    LuaScriptInstance& a = rt.make(bad);
    LuaScriptInstance& b = rt.make(good);
    ASSERT_TRUE(rt.load(a, "A"));
    ASSERT_TRUE(rt.load(b, "B"));

    for (int frame = 0; frame < 2; ++frame) {
        EXPECT_TRUE(rt.runtime.call(a, ELuaScriptCallback::Update, 0.016f));
        EXPECT_TRUE(rt.runtime.call(b, ELuaScriptCallback::Update, 0.016f));
    }
    EXPECT_EQ(rt.takeTrace(), (Trace{"bad", "good", "bad", "good"}));
    EXPECT_EQ(rt.runtime.liveCount(), 2u);
}

TEST(LuaScriptHostTest, DestroyFromOwnOnDestroyRunsOnce)
{
    FRuntime rt;
    LuaScriptInstance& instance = rt.make(rt.define("Selfish",
                                                    "local S = {}\n"
                                                    "function S:onDestroy() table.insert(TRACE, 'destroy'); destroyMe() end\n"
                                                    "return S\n"));
    rt.runtime.lua().set_function("destroyMe", [&]() { rt.runtime.destroy(instance); });
    ASSERT_TRUE(rt.load(instance, "A"));

    rt.runtime.destroy(instance);
    EXPECT_EQ(rt.takeTrace(), (Trace{"destroy"}));
    EXPECT_FALSE(instance.bLoaded);
    EXPECT_EQ(instance.runtimeId, 0u);
    EXPECT_FALSE(instance.self.valid());
    EXPECT_EQ(rt.runtime.liveCount(), 0u);
}

TEST(LuaScriptHostTest, GoneHostLeavesTheRegistry)
{
    FRuntime rt;
    const std::string path = rt.define("Plain", "local S = {}\nfunction S:onInit() table.insert(TRACE, 'init') end\nreturn S\n");
    bool              bAlive = true;
    LuaScriptInstance& kept = rt.make(path);
    LuaScriptInstance& dropped = rt.make(path);
    ASSERT_TRUE(rt.load(kept, "Kept"));
    ASSERT_TRUE(rt.load(dropped, "Dropped", &bAlive));

    bAlive = false;
    EXPECT_EQ(rt.runtime.liveCount(), 1u);
    rt.runtime.reloadScript(path);
    EXPECT_EQ(rt.takeTrace(), (Trace{"init"}));
}

// Script-to-script calls (rpg-prototype R2a): `call:<name>(args...)` with a
// return value, the face `entity:call(name, ...)` delegates to.

TEST(LuaScriptHostTest, NamedCallReturnsValue)
{
    FRuntime rt;
    const std::string path = rt.define("Echo",
                                       "local S = {}\n"
                                       "function S:concat(a, b) return a .. b end\n"
                                       "function S:half(n) return n / 2 end\n"
                                       "function S:none() end\n"
                                       "return S\n");
    LuaScriptInstance& instance = rt.make(path);
    ASSERT_TRUE(rt.load(instance, "EchoHost"));

    const std::vector<script::ScriptValue> twoArgs = {script::ScriptValue{std::string("ab")}, script::ScriptValue{std::string("cd")}};
    const script::ScriptValue              joined  = rt.runtime.callNamed(instance, "concat", twoArgs);
    ASSERT_TRUE(std::holds_alternative<std::string>(joined));
    EXPECT_EQ(std::get<std::string>(joined), "abcd");

    const std::vector<script::ScriptValue> oneArg = {script::ScriptValue{int64_t{7}}};
    const script::ScriptValue              half   = rt.runtime.callNamed(instance, "half", oneArg);
    ASSERT_TRUE(std::holds_alternative<double>(half));
    EXPECT_DOUBLE_EQ(std::get<double>(half), 3.5);

    const script::ScriptValue nothing = rt.runtime.callNamed(instance, "none", {});
    EXPECT_TRUE(std::holds_alternative<std::monostate>(nothing));
    // The host's binding ran before the call, like every lifecycle callback.
    EXPECT_EQ(instance.self.get<std::string>("hostName"), "EchoHost");
}

TEST(LuaScriptHostTest, NamedCallOnMissingFunctionIsNil)
{
    FRuntime rt;
    const std::string path = rt.define("Quiet", "local S = {}\nfunction S:onInit() end\nreturn S\n");
    LuaScriptInstance& instance = rt.make(path);
    ASSERT_TRUE(rt.load(instance, "QuietHost"));

    const std::vector<script::ScriptValue> oneArg = {script::ScriptValue{int64_t{1}}};
    const script::ScriptValue              answer = rt.runtime.callNamed(instance, "onInteract", oneArg);
    EXPECT_TRUE(std::holds_alternative<std::monostate>(answer));

    // A host that no longer resolves (destroyed instance) answers nil too.
    rt.runtime.destroy(instance);
    const script::ScriptValue gone = rt.runtime.callNamed(instance, "onInit", {});
    EXPECT_TRUE(std::holds_alternative<std::monostate>(gone));
}

TEST(LuaScriptHostTest, NamedCallPropagatesTargetErrors)
{
    FRuntime rt;
    const std::string path = rt.define("Broken",
                                       "local S = {}\n"
                                       "function S:boom() error('kaboom') end\n"
                                       "return S\n");
    LuaScriptInstance& instance = rt.make(path);
    ASSERT_TRUE(rt.load(instance, "BrokenHost"));

    EXPECT_THROW(
        {
            try {
                (void)rt.runtime.callNamed(instance, "boom", {});
            } catch (const script::ScriptError& error) {
                EXPECT_NE(std::string(error.what()).find("kaboom"), std::string::npos);
                throw;
            }
        },
        script::ScriptError);
}

} // namespace ya
