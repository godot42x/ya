// RR0 script-call microbench. DISABLED_ so the default suite does not time
// anything. Fixed iteration count, print only, no timing assertion.
//
//   xmake r ya-testing -- --gtest_also_run_disabled_tests --gtest_filter=ReflectionRuntimeMicrobench.*

#include "Core/Log.h"
#include "Core/Reflection/DeferredInitializer.h"
#include "Core/Reflection/PropertyAccessor.h"
#include "Core/Reflection/Reflection.h"
#include "Core/Scripting/ScriptBindings.h"
#include "Script/Lua/LuaTesting.h"

#include <gtest/gtest.h>

#include <array>
#include <chrono>
#include <cstdint>
#include <span>
#include <string>

namespace ya::rr0
{

struct FBench
{
    YA_REFLECT_BEGIN(FBench)
    YA_REFLECT_FIELD(scalar)
    YA_REFLECT_METHOD(scaled)
    YA_REFLECT_END()

    float scalar = 1.0f;

    [[nodiscard]] float scaled(float factor) const { return scalar * factor; }
};

namespace
{

constexpr int kIterations = 100000;
constexpr int kWarmup     = 1000;

FBench& benchObject()
{
    static FBench object;
    return object;
}

uint32_t benchKind()
{
    static const uint32_t id = script::registerRefKind(script::ScriptRefKind{
        .name    = "rr0-bench",
        .resolve = [](const script::ScriptRef&) -> void* { return &benchObject(); },
    });
    return id;
}

void prepareBench()
{
    static const bool once = [] {
        FBench::__ensure_reflection_registered();
        reflection::DeferredInitializerQueue::instance().executeAll();
        script::addMethod(type_index_v<FBench>, "bump", script::fn([](FBench& self, int extra) {
                              return static_cast<int>(self.scalar) + extra;
                          }));
        return true;
    }();
    (void)once;
}

[[nodiscard]] double nanosecondsPerOp(std::chrono::steady_clock::duration elapsed)
{
    const double ns = std::chrono::duration<double, std::nano>(elapsed).count();
    return ns / static_cast<double>(kIterations);
}

void report(const char* name, double nsPerOp)
{
    YA_CORE_INFO("RR0 microbench {} {:.1f} ns/op ({} iters)", name, nsPerOp, kIterations);
    ::testing::Test::RecordProperty(name, std::to_string(nsPerOp));
}

} // namespace

TEST(ReflectionRuntimeMicrobench, DISABLED_LuaNativeCall)
{
    prepareBench();
    sol::state lua;
    lua.open_libraries(sol::lib::base);
    registerLuaScriptBindings(lua);
    lua["obj"] = LuaScriptObject{script::ScriptRef{.type = type_index_v<FBench>, .kind = benchKind(), .a = 1}};
    ASSERT_TRUE(lua.safe_script("function bench(n) return obj:bump(n) end", sol::script_pass_on_error).valid());
    sol::protected_function call = lua["bench"];
    for (int i = 0; i < kWarmup; ++i) {
        (void)call(1);
    }
    const auto start = std::chrono::steady_clock::now();
    volatile int64_t sink = 0;
    for (int i = 0; i < kIterations; ++i) {
        sink = call(1).get<int64_t>();
    }
    report("lua_native_ns", nanosecondsPerOp(std::chrono::steady_clock::now() - start));
    (void)sink;
}

TEST(ReflectionRuntimeMicrobench, DISABLED_LuaPropertyReadWrite)
{
    prepareBench();
    sol::state lua;
    lua.open_libraries(sol::lib::base);
    registerLuaScriptBindings(lua);
    lua["obj"] = LuaScriptObject{script::ScriptRef{.type = type_index_v<FBench>, .kind = benchKind(), .a = 1}};
    ASSERT_TRUE(lua.safe_script("function bench(v) obj.scalar = v return obj.scalar end", sol::script_pass_on_error).valid());
    sol::protected_function call = lua["bench"];
    for (int i = 0; i < kWarmup; ++i) {
        (void)call(1.0);
    }
    const auto start = std::chrono::steady_clock::now();
    volatile double sink = 0.0;
    for (int i = 0; i < kIterations; ++i) {
        sink = call(i % 2 == 0 ? 1.0 : 2.0).get<double>();
    }
    report("lua_property_ns", nanosecondsPerOp(std::chrono::steady_clock::now() - start));
    (void)sink;
}

TEST(ReflectionRuntimeMicrobench, DISABLED_FunctionInvoker)
{
    prepareBench();
    Class*    cls = ClassRegistry::instance().getClass(type_index_v<FBench>);
    ASSERT_NE(cls, nullptr);
    const Function& function = cls->functions().at("scaled");
    FBench&   object   = benchObject();
    for (int i = 0; i < kWarmup; ++i) {
        std::array<refl::MetaValue, 1> args{refl::MetaValue::make(2.0f)};
        (void)function.invoker(&object, args);
    }
    const auto start = std::chrono::steady_clock::now();
    volatile float sink = 0.0f;
    for (int i = 0; i < kIterations; ++i) {
        std::array<refl::MetaValue, 1> args{refl::MetaValue::make(2.0f)};
        sink = *function.invoker(&object, args).tryGet<float>();
    }
    report("invoker_ns", nanosecondsPerOp(std::chrono::steady_clock::now() - start));
    (void)sink;
}

TEST(ReflectionRuntimeMicrobench, DISABLED_PropertyAccessor)
{
    prepareBench();
    Class* cls = ClassRegistry::instance().getClass(type_index_v<FBench>);
    ASSERT_NE(cls, nullptr);
    const reflection::FPropertySlot slot(cls->properties().at("scalar"));
    FBench&                         object = benchObject();
    for (int i = 0; i < kWarmup; ++i) {
        float value = 0.0f;
        (void)reflection::PropertyAccessor::tryGet(slot, &object, value);
        (void)reflection::PropertyAccessor::set(slot, &object, value == 1.0f ? 2.0f : 1.0f);
    }
    const auto start = std::chrono::steady_clock::now();
    volatile float sink = 0.0f;
    for (int i = 0; i < kIterations; ++i) {
        float value = 0.0f;
        (void)reflection::PropertyAccessor::tryGet(slot, &object, value);
        sink = value;
        (void)reflection::PropertyAccessor::set(slot, &object, value == 1.0f ? 2.0f : 1.0f);
    }
    report("accessor_ns", nanosecondsPerOp(std::chrono::steady_clock::now() - start));
    (void)sink;
}

} // namespace ya::rr0
