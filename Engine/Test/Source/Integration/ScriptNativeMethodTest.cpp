// script::fn signature deduction, ClassRegistry injection, and the direct trampoline.

#include "Core/Log.h"
#include "Core/Reflection/Reflection.h"
#include "Core/Scripting/ScriptBindings.h"

#include <gtest/gtest.h>

#include <glm/mat4x4.hpp>

#include <any>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace ya::script_native_test
{

struct FNativeProbe
{
    YA_REFLECT_BEGIN(FNativeProbe)
    YA_REFLECT_METHOD(echo)
    YA_REFLECT_END()

    int echo(int value) const { return value + 1; }
};

struct FOrderProbe
{
    YA_REFLECT_BEGIN(FOrderProbe)
    YA_REFLECT_END()

    void wider(const std::string&, float) {}
};

// `script::fn` static_asserts `nativeSignatureOk`, so a signature that fails
// these predicates is a compile error. The predicates are the contract.
static_assert(script::crossesToScripts<std::string>());
static_assert(script::crossesToScripts<int>());
static_assert(script::crossesToScripts<script::ScriptSelfHandle>());
static_assert(!script::crossesToScripts<glm::mat4>());
static_assert(script::nativeSignatureOk<std::string, FNativeProbe&, const std::string&, int>());
static_assert(script::nativeSignatureOk<int64_t, FNativeProbe&, script::ScriptSelfHandle, int>());
static_assert(script::nativeOwnerCount<script::ScriptSelfHandle, int>() == 1);
static_assert(script::nativeOwnerCount<script::ScriptSelfHandle, script::ScriptSelfHandle>() == 2);
static_assert(script::nativeOwnerSlot<FNativeProbe&, script::ScriptSelfHandle, int>() == 0);
static_assert(script::nativeOwnerSlot<FNativeProbe&, const std::string&>() == script::kNoOwnerSlot);
static_assert(!script::nativeSignatureOk<void, FNativeProbe&, glm::mat4>());
static_assert(!script::nativeSignatureOk<void, FNativeProbe&, glm::mat4&>());
static_assert(!script::nativeSignatureOk<void, script::ScriptSelfHandle, script::ScriptSelfHandle>());
// ScriptValue is not a variant arm of itself, so it still cannot be a parameter.
static_assert(!script::crossesToScripts<script::ScriptValue>());
static_assert(!script::nativeSignatureOk<void, script::ScriptValue>());

std::string labelOf(FNativeProbe&, const std::string& prefix, int n)
{
    return prefix + std::to_string(n);
}

int64_t withOwner(FNativeProbe&, script::ScriptSelfHandle owner, int extra)
{
    return static_cast<int64_t>(owner.runtimeId) + extra;
}

struct WarnCounter
{
    std::string                        needle;
    int                                count = 0;
    logcc::LoggerBase::formatter_t     previous;

    explicit WarnCounter(std::string_view text) : needle(text)
    {
        auto& logger = Logger::core();
        previous     = logger.formatter;
        logger.setFormatter([this](const logcc::Config& config, std::string& output, logcc::LogLevel::T level,
                                   std::string_view message, const std::source_location& location) {
            if (level == logcc::LogLevel::Warn && message.find(needle) != std::string_view::npos) {
                ++count;
            }
            return previous ? previous(config, output, level, message, location) : true;
        });
    }

    ~WarnCounter() { Logger::core().setFormatter(std::move(previous)); }
};

struct ProbeSetup
{
    int rejectedEcho = 0;
};

ProbeSetup& probeSetup()
{
    static ProbeSetup setup = [] {
        FNativeProbe::__ensure_reflection_registered();
        ProbeSetup out;
        const type_index_t type = type_index_v<FNativeProbe>;
        {
            WarnCounter warns("keeps the first registration");
            script::addMethod(type, "echo", script::fn([](FNativeProbe&, const std::string&) { return 0; }));
            out.rejectedEcho = warns.count;
        }
        script::addMethod(type, "labelOf", script::fn(&labelOf));
        script::addMethod(type, "withOwner", script::fn(&withOwner));
        return out;
    }();
    return setup;
}

FNativeProbe& probeObject()
{
    static FNativeProbe probe;
    return probe;
}

uint32_t probeKind()
{
    static const uint32_t id = script::registerRefKind(script::ScriptRefKind{
        .name    = "native-probe",
        .resolve = [](const script::ScriptRef&) -> void* { return &probeObject(); },
    });
    return id;
}

script::ScriptRef probeRef()
{
    return script::ScriptRef{.type = type_index_v<FNativeProbe>, .kind = probeKind(), .a = 1};
}

TEST(ScriptNativeMethod, HandleCodecsCrossAtRuntime)
{
    EXPECT_TRUE(script::crossesToScripts(type_index_v<script::ScriptSelfHandle>));
    EXPECT_TRUE(script::crossesToScripts(type_index_v<script::ScriptFnHandle>));
    EXPECT_TRUE(script::crossesToScripts(type_index_v<script::ScriptRect>));
    EXPECT_FALSE(script::crossesToScripts(type_index_v<script::ScriptValue>));
}

TEST(ScriptNativeMethod, FirstRegistrationWins)
{
    FOrderProbe::__ensure_reflection_registered();
    const type_index_t type = type_index_v<FOrderProbe>;
    Class*             cls  = ClassRegistry::instance().getClass(type);
    ASSERT_NE(cls, nullptr);

    script::addMethod(type, "kept", script::fn([](FOrderProbe&, int value) { return value; }));
    const std::vector<refl::ParamDesc> params = cls->functions.at("kept").args;
    ASSERT_EQ(params.size(), 1u);
    EXPECT_EQ(params[0].type, type_index_v<int>);
    EXPECT_FALSE(params[0].bRef);
    EXPECT_FALSE(params[0].bPointer);

    {
        WarnCounter warns("keeps the first registration");
        cls->function("kept", &FOrderProbe::wider);
        EXPECT_GE(warns.count, 1);
    }
    EXPECT_EQ(cls->functions.at("kept").args.size(), 1u);
    EXPECT_EQ(cls->functions.at("kept").args[0].type, type_index_v<int>);

    // Reflection registered `echo` at startup. The later native injection keeps it.
    EXPECT_GE(probeSetup().rejectedEcho, 1);
    Class* probeClass = ClassRegistry::instance().getClass(type_index_v<FNativeProbe>);
    ASSERT_NE(probeClass, nullptr);
    ASSERT_EQ(probeClass->functions.at("echo").args.size(), 1u);
    EXPECT_EQ(probeClass->functions.at("echo").args[0].type, type_index_v<int>);
}

TEST(ScriptNativeMethod, ExportsNativeAndReflectedFromTheRegistry)
{
    const type_index_t type = type_index_v<FNativeProbe>;
    (void)probeSetup();
    Class* cls = ClassRegistry::instance().getClass(type);
    ASSERT_NE(cls, nullptr);

    bool sawEcho  = false;
    bool sawLabel = false;
    bool sawOwner = false;
    for (const auto& [name, function] : cls->functions) {
        if (name == "echo") {
            sawEcho = function.args.size() == 1 && function.args[0].type == type_index_v<int>;
        }
        else if (name == "labelOf") {
            sawLabel = function.args.size() == 2 && function.args[0].type == type_index_v<std::string> &&
                       function.args[1].type == type_index_v<int> && function.ret.type == type_index_v<std::string>;
        }
        else if (name == "withOwner") {
            sawOwner = function.args.size() == 2 && function.args[0].type == type_index_v<script::ScriptSelfHandle> &&
                       function.args[1].type == type_index_v<int>;
        }
    }
    EXPECT_TRUE(sawEcho);
    EXPECT_TRUE(sawLabel);
    EXPECT_TRUE(sawOwner);

    const Function& label = cls->functions.at("labelOf");
    refl::MetaValue boxed[] = {refl::MetaValue::make(std::string("id")), refl::MetaValue::make(4)};
    const refl::MetaValue labeledValue = label.invoker(&probeObject(), boxed);
    ASSERT_NE(labeledValue.tryGet<std::string>(), nullptr);
    EXPECT_EQ(*labeledValue.tryGet<std::string>(), "id4");

    const script::ScriptMethod* labeled = script::findMethod(type, "labelOf");
    const script::ScriptMethod* echoed  = script::findMethod(type, "echo");
    const script::ScriptMethod* owned   = script::findMethod(type, "withOwner");
    ASSERT_NE(labeled, nullptr);
    ASSERT_NE(echoed, nullptr);
    ASSERT_NE(owned, nullptr);
    // methodArgumentType reads Function::args, so both kinds were exported
    // from the registry rather than from a native-only side table.
    EXPECT_EQ(script::methodArgumentType(*labeled, 0), type_index_v<std::string>);
    EXPECT_EQ(script::methodArgumentType(*labeled, 1), type_index_v<int>);
    EXPECT_EQ(script::methodArgumentType(*echoed, 0), type_index_v<int>);
    EXPECT_EQ(script::methodArgumentType(*owned, 0), type_index_v<script::ScriptSelfHandle>);
    EXPECT_EQ(script::methodArgumentType(*owned, 1), type_index_v<int>);

    const script::ScriptRef   ref        = probeRef();
    const script::ScriptValue echoArgs[] = {int64_t{2}};
    EXPECT_EQ(script::callMethod(ref, "echo", echoArgs), script::ScriptValue{int64_t{3}});
    auto echoInvoker = cls->functions.at("echo").invoker;
    cls->functions.at("echo").invoker = [](void*, std::span<refl::MetaValue>) -> refl::MetaValue {
        throw std::runtime_error("boxed invoker");
    };
    EXPECT_THROW(script::callMethod(ref, "echo", echoArgs), std::runtime_error);
    cls->functions.at("echo").invoker = std::move(echoInvoker);

    // script::fn is an ordinary Function: callMethod goes through invoker.
    auto labelInvoker = cls->functions.at("labelOf").invoker;
    cls->functions.at("labelOf").invoker = [](void*, std::span<refl::MetaValue>) -> refl::MetaValue {
        throw std::runtime_error("boxed invoker");
    };
    const script::ScriptValue labelArgs[] = {std::string("id"), int64_t{4}};
    EXPECT_THROW(script::callMethod(ref, "labelOf", labelArgs), std::runtime_error);
    cls->functions.at("labelOf").invoker = std::move(labelInvoker);

    const script::ScriptValue ownerArgs[] = {script::ScriptSelfHandle{.runtimeId = 7}, int64_t{3}};
    EXPECT_EQ(script::callMethod(ref, "withOwner", ownerArgs), script::ScriptValue{int64_t{10}});
    EXPECT_THROW(script::callMethod(ref, "labelOf", {}), script::ScriptError);
}

} // namespace ya::script_native_test
