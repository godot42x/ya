// RR0 characterization. Locks the value-type behavior that later TypeOps work
// must keep. Assertions describe what the code does today, including lossy
// and inconsistent paths. They are not a wish list.

#include "Core/Common/AssetRef.h"
#include "Core/Log.h"
#include "Core/Math/GLM.h"
#include "Core/Reflection/DeferredInitializer.h"
#include "Core/Reflection/PropertyAccessor.h"
#include "Core/Reflection/Reflection.h"
#include "Core/Reflection/ReflectionCopier.h"
#include "Core/Reflection/ReflectionSerializer.h"
#include "Core/Scripting/ScriptBindings.h"
#include "Core/System/VirtualFileSystem.h"
#include "ECS/ComponentJson.h"
#include "ECS/ECSRegistry.h"
#include "ECS/Systems/Components/LuaScriptComponent.h"
#include "GameEditor/Inspector/PropertyEditorMetadata.h"
#include "GameEditor/Inspector/PropertyGraph.h"
#include "Script/Lua/LuaTesting.h"

#include <gtest/gtest.h>

#include <glm/gtc/quaternion.hpp>
#include <glm/mat4x4.hpp>

#include <cstdint>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace ya::rr0
{

enum class EBaselineMood : uint8_t
{
    Calm = 0,
    Angry = 1,
};

struct FBaseline
{
    YA_REFLECT_BEGIN(FBaseline)
    YA_REFLECT_FIELD(flag)
    YA_REFLECT_FIELD(signedInt)
    YA_REFLECT_FIELD(code)
    YA_REFLECT_FIELD(wide)
    YA_REFLECT_FIELD(tiny)
    YA_REFLECT_FIELD(half)
    YA_REFLECT_FIELD(ubyte)
    YA_REFLECT_FIELD(ushort)
    YA_REFLECT_FIELD(span)
    YA_REFLECT_FIELD(ulong)
    YA_REFLECT_FIELD(longlong)
    YA_REFLECT_FIELD(scalar, .manipulate(0.0f, 1.0f))
    YA_REFLECT_FIELD(precise)
    YA_REFLECT_FIELD(label)
    YA_REFLECT_FIELD(offset)
    YA_REFLECT_FIELD(position)
    YA_REFLECT_FIELD(tint, .color())
    YA_REFLECT_FIELD(mood)
    YA_REFLECT_FIELD(texture)
    YA_REFLECT_FIELD(numbers)
    YA_REFLECT_FIELD(points)
    YA_REFLECT_FIELD(reals)
    YA_REFLECT_FIELD(precisions)
    YA_REFLECT_FIELD(tags)
    YA_REFLECT_FIELD(flags)
    YA_REFLECT_FIELD(byName)
    YA_REFLECT_FIELD(byCode)
    YA_REFLECT_FIELD(byUnsigned)
    YA_REFLECT_FIELD(orientation)
    YA_REFLECT_FIELD(matrix)
    YA_REFLECT_FIELD(rect)
    YA_REFLECT_FIELD(raw)
    YA_REFLECT_METHOD(observe)
    YA_REFLECT_METHOD(poke)
    YA_REFLECT_METHOD(writeBack)
    YA_REFLECT_METHOD(scaled)
    YA_REFLECT_END()

    bool                            flag       = false;
    int                             signedInt  = 0;
    uint32_t                        code       = 0;
    int64_t                         wide       = 0;
    int8_t                          tiny       = 0;
    int16_t                         half       = 0;
    uint8_t                         ubyte      = 0;
    uint16_t                        ushort     = 0;
    long                            span       = 0;
    unsigned long                   ulong      = 0;
    long long                       longlong   = 0;
    float                           scalar     = 0.0f;
    double                          precise    = 0.0;
    std::string                     label;
    glm::vec2                       offset{0.0f};
    glm::vec3                       position{0.0f};
    glm::vec4                       tint{0.0f};
    EBaselineMood                   mood = EBaselineMood::Calm;
    TextureRef                      texture;
    std::vector<int>                numbers;
    std::vector<glm::vec3>          points;
    std::vector<float>              reals;
    std::vector<double>             precisions;
    std::vector<std::string>        tags;
    std::set<bool>                  flags;
    std::map<std::string, float>    byName;
    std::map<int, int>              byCode;
    std::map<uint32_t, int>         byUnsigned;
    glm::quat                       orientation{};
    glm::mat4                       matrix{1.0f};
    script::ScriptRect              rect{};
    int*                            raw = nullptr;

    bool boundCopy = false;
    int  lastSeen  = 0;

    void observe(const int& value)
    {
        boundCopy = &value != &signedInt;
        lastSeen  = value;
    }

    void poke(int* pointer)
    {
        if (pointer) {
            *pointer = 9;
        }
    }

    void writeBack(int& slot) { slot = signedInt; }

    [[nodiscard]] float scaled(float factor) const { return scalar * factor; }
};

} // namespace ya::rr0

YA_REFLECT_ENUM_BEGIN(ya::rr0::EBaselineMood)
YA_REFLECT_ENUM_VALUE(Calm)
YA_REFLECT_ENUM_VALUE(Angry)
YA_REFLECT_ENUM_END()

namespace ya::rr0
{
namespace
{

int gHeld = 11;

FBaseline& sample()
{
    static FBaseline value = [] {
        FBaseline out;
        out.flag       = true;
        out.signedInt  = 7;
        out.code       = 9;
        out.wide       = 64;
        out.tiny       = 3;
        out.half       = 16;
        out.ubyte      = 8;
        out.ushort     = 1600;
        out.span       = 64;
        out.ulong      = 128;
        out.longlong   = 256;
        out.scalar     = 1.5f;
        out.precise    = 2.25;
        out.label      = "hero";
        out.offset     = {4.0f, 5.0f};
        out.position   = {1.0f, 2.0f, 3.0f};
        out.tint       = {0.25f, 0.5f, 0.75f, 1.0f};
        out.mood       = EBaselineMood::Angry;
        out.texture.setPath("Content/hero.png");
        out.numbers    = {1, 2};
        out.points     = {glm::vec3{1.0f, 0.0f, 0.0f}};
        out.reals      = {0.5f};
        out.precisions = {1.25};
        out.tags       = {"a"};
        out.flags      = {false, true};
        out.byName     = {{"hp", 1.5f}};
        out.byCode     = {{4, 5}};
        out.byUnsigned = {{8u, 6}};
        out.orientation = glm::quat(0.0f, 0.0f, 0.0f, 1.0f);
        out.matrix      = glm::mat4(2.0f);
        out.rect        = {1.0f, 2.0f, 3.0f, 4.0f};
        out.raw         = nullptr;
        return out;
    }();
    return value;
}

void prepare()
{
    static const bool once = [] {
        if (!VirtualFileSystem::get()) {
            VirtualFileSystem::init();
        }
        FBaseline::__ensure_reflection_registered();
        reflection::DeferredInitializerQueue::instance().executeAll();
        script::addMethod(type_index_v<FBaseline>, "bump", script::fn([](FBaseline& self, int extra) {
                              return self.signedInt + extra;
                          }));
        return true;
    }();
    (void)once;
}

struct FWorld
{
    static FWorld& get()
    {
        static FWorld world;
        return world;
    }

    static uint32_t kind()
    {
        static const uint32_t id = script::registerRefKind(script::ScriptRefKind{
            .name    = "rr0-baseline",
            .resolve = [](const script::ScriptRef&) -> void* { return &sample(); },
        });
        return id;
    }
};

script::ScriptRef objectRef()
{
    return script::ScriptRef{.type = type_index_v<FBaseline>, .kind = FWorld::kind(), .a = 1};
}

const Property& field(const char* name)
{
    Class* cls = ClassRegistry::instance().getClass(type_index_v<FBaseline>);
    EXPECT_NE(cls, nullptr);
    return cls->properties.at(name);
}

reflection::FPropertySlot slotOf(const char* name)
{
    return reflection::FPropertySlot::field(field(name));
}

} // namespace

TEST(ReflectionRuntimeBaseline, IntegerAliasesOnThisCompiler)
{
    // Apple Clang LP64 pretty-prints typedefs as the underlying type, so
    // these hashes collapse. The if-chains that mention both spellings are
    // the same branch here.
    EXPECT_EQ(type_index_v<int>, type_index_v<int32_t>);
    EXPECT_EQ(type_index_v<unsigned int>, type_index_v<uint32_t>);
    // `int64_t` keeps that spelling in __PRETTY_FUNCTION__; `long` does not.
    EXPECT_NE(type_index_v<long>, type_index_v<int64_t>);
    EXPECT_NE(type_index_v<int>, type_index_v<long>);
    EXPECT_NE(type_index_v<long long>, type_index_v<long>);
    EXPECT_NE(type_index_v<int8_t>, type_index_v<int>);
    EXPECT_NE(type_index_v<int16_t>, type_index_v<int>);
    EXPECT_NE(type_index_v<uint8_t>, type_index_v<unsigned int>);
    EXPECT_NE(type_index_v<uint16_t>, type_index_v<unsigned int>);
}

TEST(ReflectionRuntimeBaseline, JsonExactShapeAndRoundTrip)
{
    prepare();
    FBaseline&         src  = sample();
    const nlohmann::json json = ReflectionSerializer::serializeByRuntimeReflection(src);

    EXPECT_EQ(json["flag"].dump(), "true");
    EXPECT_EQ(json["signedInt"].dump(), "7");
    EXPECT_EQ(json["code"].dump(), "9");
    EXPECT_EQ(json["scalar"].dump(), "1.5");
    EXPECT_EQ(json["precise"].dump(), "2.25");
    EXPECT_EQ(json["label"].dump(), "\"hero\"");
    EXPECT_EQ(json["offset"].dump(), "[4.0,5.0]");
    EXPECT_EQ(json["position"].dump(), "[1.0,2.0,3.0]");
    EXPECT_EQ(json["tint"].dump(), "[0.25,0.5,0.75,1.0]");
    EXPECT_EQ(json["mood"].dump(), "\"Angry\"");
    EXPECT_EQ(json["texture"].dump(), R"({"__base__":{"AssetRefBase":{"_path":"Content/hero.png"}}})");
    EXPECT_EQ(json["numbers"].dump(), "[1,2]");
    EXPECT_EQ(json["points"].dump(), "[[1.0,0.0,0.0]]");
    EXPECT_EQ(json["reals"].dump(), "[0.5]");
    EXPECT_EQ(json["precisions"].dump(), "[1.25]");
    EXPECT_EQ(json["tags"].dump(), "[\"a\"]");
    EXPECT_EQ(json["flags"].dump(), "[false,true]");
    EXPECT_EQ(json["byName"].dump(), R"({"hp":1.5})");
    EXPECT_EQ(json["byCode"].dump(), R"({"4":5})");

    // Widths the scalar if-chain does not name become JSON null. uint32 map
    // keys throw inside the serializer and the property is dropped.
    for (const char* name : {"wide", "tiny", "half", "ubyte", "ushort", "span", "ulong", "longlong", "orientation", "matrix", "rect"}) {
        EXPECT_EQ(json[name].dump(), "null") << name;
    }
    EXPECT_FALSE(json.contains("byUnsigned"));
    EXPECT_EQ(json["raw"].dump(), "null");

    FBaseline restored;
    ReflectionSerializer::deserializeByRuntimeReflection(restored, json, "");
    EXPECT_TRUE(restored.flag);
    EXPECT_EQ(restored.signedInt, 7);
    EXPECT_EQ(restored.code, 9u);
    EXPECT_FLOAT_EQ(restored.scalar, 1.5f);
    EXPECT_DOUBLE_EQ(restored.precise, 2.25);
    EXPECT_EQ(restored.label, "hero");
    EXPECT_EQ(restored.offset, glm::vec2(4.0f, 5.0f));
    EXPECT_EQ(restored.position, glm::vec3(1.0f, 2.0f, 3.0f));
    EXPECT_EQ(restored.tint, glm::vec4(0.25f, 0.5f, 0.75f, 1.0f));
    EXPECT_EQ(restored.mood, EBaselineMood::Angry);
    EXPECT_EQ(restored.texture.getPath(), "Content/hero.png");
    EXPECT_EQ(restored.numbers, src.numbers);
    EXPECT_EQ(restored.points, src.points);
    EXPECT_EQ(restored.reals, src.reals);
    EXPECT_EQ(restored.precisions, src.precisions);
    EXPECT_EQ(restored.tags, src.tags);
    EXPECT_EQ(restored.flags, src.flags);
    EXPECT_EQ(restored.byName, src.byName);
    EXPECT_EQ(restored.byCode, src.byCode);

    // Unsupported fields are stored as null and do not come back.
    EXPECT_EQ(restored.wide, 0);
    EXPECT_EQ(restored.tiny, 0);
    EXPECT_EQ(restored.half, 0);
    EXPECT_EQ(restored.ubyte, 0);
    EXPECT_EQ(restored.ushort, 0);
    EXPECT_EQ(restored.span, 0);
    EXPECT_EQ(restored.ulong, 0u);
    EXPECT_EQ(restored.longlong, 0);
    EXPECT_EQ(restored.orientation, glm::quat{});
    // null JSON does not write the field, so the destination keeps its default
    // (identity), not the source's mat4(2).
    EXPECT_EQ(restored.matrix, glm::mat4(1.0f));
    EXPECT_NE(restored.matrix, src.matrix);
    EXPECT_EQ(restored.rect, script::ScriptRect{});
    EXPECT_TRUE(restored.byUnsigned.empty());
    EXPECT_EQ(restored.raw, nullptr);

    // A non-empty uint32-keyed map throws and the property is omitted. After
    // that drop the destination map is empty, and an empty map serializes as {}.
    EXPECT_FALSE(json.contains("byUnsigned"));
    nlohmann::json second = ReflectionSerializer::serializeByRuntimeReflection(restored);
    EXPECT_TRUE(second.contains("byUnsigned"));
    EXPECT_TRUE(second["byUnsigned"].empty());
    second.erase("byUnsigned");
    EXPECT_EQ(second.dump(), json.dump());

    // The optional template unwraps, then the class serializer runs. float is
    // not a class, so both empty and engaged optionals come back as null, and
    // a numeric payload deserializes as 0.
    std::optional<float> empty;
    std::optional<float> present = 1.5f;
    EXPECT_TRUE(ReflectionSerializer::serializeByRuntimeReflection(empty).is_null());
    EXPECT_TRUE(ReflectionSerializer::serializeByRuntimeReflection(present).is_null());
    std::optional<float> revived;
    ReflectionSerializer::deserializeByRuntimeReflection(revived, nlohmann::json(1.5f), "");
    ASSERT_TRUE(revived.has_value());
    EXPECT_FLOAT_EQ(*revived, 0.0f);
}

TEST(ReflectionRuntimeBaseline, PointerMemberSerializesPointeeButDoesNotAllocate)
{
    prepare();
    FBaseline src;
    src.raw = &gHeld;
    const nlohmann::json json = ReflectionSerializer::serializeByRuntimeReflection(src);
    EXPECT_EQ(json["raw"].dump(), "11");

    FBaseline restored;
    ReflectionSerializer::deserializeByRuntimeReflection(restored, json, "");
    // int is not a reflected class, so the pointer is not rebuilt.
    EXPECT_EQ(restored.raw, nullptr);
}

TEST(ReflectionRuntimeBaseline, PropertyAccessorReadsAndWritesLeafTypes)
{
    prepare();
    FBaseline& obj = sample();

    bool flag = false;
    ASSERT_TRUE(reflection::PropertyAccessor::tryGet(slotOf("flag"), &obj, flag));
    EXPECT_TRUE(flag);
    EXPECT_TRUE(reflection::PropertyAccessor::set(slotOf("flag"), &obj, false));
    EXPECT_FALSE(obj.flag);
    EXPECT_TRUE(reflection::PropertyAccessor::set(slotOf("flag"), &obj, true));

    int64_t integer = 0;
    ASSERT_TRUE(reflection::PropertyAccessor::tryGetInteger(slotOf("signedInt"), &obj, integer));
    EXPECT_EQ(integer, 7);
    EXPECT_TRUE(reflection::PropertyAccessor::setInteger(slotOf("signedInt"), &obj, 8));
    EXPECT_EQ(obj.signedInt, 8);
    EXPECT_TRUE(reflection::PropertyAccessor::setInteger(slotOf("signedInt"), &obj, 7));
    EXPECT_FALSE(reflection::PropertyAccessor::tryGetInteger(slotOf("precise"), &obj, integer));

    ASSERT_TRUE(reflection::PropertyAccessor::tryGetInteger(slotOf("code"), &obj, integer));
    EXPECT_EQ(integer, 9);
    EXPECT_TRUE(reflection::PropertyAccessor::setInteger(slotOf("code"), &obj, 10));
    EXPECT_EQ(obj.code, 10u);
    EXPECT_TRUE(reflection::PropertyAccessor::setInteger(slotOf("code"), &obj, 9));

    float scalar = 0.0f;
    ASSERT_TRUE(reflection::PropertyAccessor::tryGet(slotOf("scalar"), &obj, scalar));
    EXPECT_FLOAT_EQ(scalar, 1.5f);
    EXPECT_TRUE(reflection::PropertyAccessor::set(slotOf("scalar"), &obj, 2.5f));
    EXPECT_FLOAT_EQ(obj.scalar, 2.5f);
    EXPECT_TRUE(reflection::PropertyAccessor::set(slotOf("scalar"), &obj, 1.5f));

    double precise = 0.0;
    ASSERT_TRUE(reflection::PropertyAccessor::tryGet(slotOf("precise"), &obj, precise));
    EXPECT_DOUBLE_EQ(precise, 2.25);
    // Generic typed get/set works. equals() has no double branch, so two
    // equal doubles still compare unequal.
    double copy = precise;
    EXPECT_FALSE(reflection::PropertyAccessor::equals(slotOf("precise"), &obj.precise, &copy));

    std::string label;
    ASSERT_TRUE(reflection::PropertyAccessor::tryGet(slotOf("label"), &obj, label));
    EXPECT_EQ(label, "hero");
    EXPECT_TRUE(reflection::PropertyAccessor::set(slotOf("label"), &obj, std::string("side")));
    EXPECT_EQ(obj.label, "side");
    EXPECT_TRUE(reflection::PropertyAccessor::set(slotOf("label"), &obj, std::string("hero")));

    glm::vec2 offset{};
    glm::vec3 position{};
    glm::vec4 tint{};
    ASSERT_TRUE(reflection::PropertyAccessor::tryGet(slotOf("offset"), &obj, offset));
    ASSERT_TRUE(reflection::PropertyAccessor::tryGet(slotOf("position"), &obj, position));
    ASSERT_TRUE(reflection::PropertyAccessor::tryGetColor(slotOf("tint"), &obj, tint));
    EXPECT_EQ(offset, glm::vec2(4.0f, 5.0f));
    EXPECT_EQ(position, glm::vec3(1.0f, 2.0f, 3.0f));
    EXPECT_EQ(tint, obj.tint);

    int64_t mood = 0;
    ASSERT_TRUE(reflection::PropertyAccessor::tryGetEnumValue(slotOf("mood"), &obj, mood));
    EXPECT_EQ(mood, static_cast<int64_t>(EBaselineMood::Angry));
    EXPECT_TRUE(reflection::PropertyAccessor::setEnumValue(slotOf("mood"), &obj, static_cast<int64_t>(EBaselineMood::Calm)));
    EXPECT_EQ(obj.mood, EBaselineMood::Calm);
    EXPECT_TRUE(reflection::PropertyAccessor::setEnumValue(slotOf("mood"), &obj, static_cast<int64_t>(EBaselineMood::Angry)));

    std::string path;
    ASSERT_TRUE(reflection::PropertyAccessor::tryGetAssetPath(slotOf("texture"), &obj, path));
    EXPECT_EQ(path, "Content/hero.png");
    EXPECT_TRUE(reflection::PropertyAccessor::setAssetPath(slotOf("texture"), &obj, "Content/other.png"));
    EXPECT_EQ(obj.texture.getPath(), "Content/other.png");
    EXPECT_TRUE(reflection::PropertyAccessor::setAssetPath(slotOf("texture"), &obj, "Content/hero.png"));

    EXPECT_TRUE(reflection::PropertyAccessor::isLeafValueType(type_index_v<bool>));
    EXPECT_TRUE(reflection::PropertyAccessor::isLeafValueType(type_index_v<int>));
    EXPECT_TRUE(reflection::PropertyAccessor::isLeafValueType(type_index_v<uint32_t>));
    EXPECT_TRUE(reflection::PropertyAccessor::isLeafValueType(type_index_v<float>));
    EXPECT_TRUE(reflection::PropertyAccessor::isLeafValueType(type_index_v<std::string>));
    EXPECT_TRUE(reflection::PropertyAccessor::isLeafValueType(type_index_v<glm::vec2>));
    EXPECT_TRUE(reflection::PropertyAccessor::isLeafValueType(type_index_v<glm::vec3>));
    EXPECT_TRUE(reflection::PropertyAccessor::isLeafValueType(type_index_v<glm::vec4>));
    EXPECT_TRUE(reflection::PropertyAccessor::isLeafValueType(type_index_v<EBaselineMood>));
    EXPECT_TRUE(reflection::PropertyAccessor::isLeafValueType(type_index_v<TextureRef>));
    EXPECT_FALSE(reflection::PropertyAccessor::isLeafValueType(type_index_v<double>));
    EXPECT_FALSE(reflection::PropertyAccessor::isLeafValueType(type_index_v<int64_t>));
    EXPECT_FALSE(reflection::PropertyAccessor::isLeafValueType(type_index_v<int8_t>));
    EXPECT_FALSE(reflection::PropertyAccessor::isLeafValueType(type_index_v<glm::quat>));
    EXPECT_FALSE(reflection::PropertyAccessor::isLeafValueType(type_index_v<glm::mat4>));
    EXPECT_FALSE(reflection::PropertyAccessor::isLeafValueType(type_index_v<script::ScriptRect>));

    // Typed get still works for a non-leaf the if-chain ignores.
    glm::quat orientation{};
    ASSERT_TRUE(reflection::PropertyAccessor::tryGet(slotOf("orientation"), &obj, orientation));
    EXPECT_EQ(orientation, obj.orientation);
}

TEST(ReflectionRuntimeBaseline, CopierCopiesScalarsVectorsAndAssetPaths)
{
    prepare();
    FBaseline& src = sample();
    FBaseline  dst;
    dst.matrix = glm::mat4(1.0f);
    ASSERT_TRUE(ReflectionCopier::copyByRuntimeReflection(&dst, &src, type_index_v<FBaseline>, "FBaseline"));

    EXPECT_EQ(dst.flag, src.flag);
    EXPECT_EQ(dst.signedInt, src.signedInt);
    EXPECT_EQ(dst.code, src.code);
    EXPECT_FLOAT_EQ(dst.scalar, src.scalar);
    EXPECT_DOUBLE_EQ(dst.precise, src.precise);
    EXPECT_EQ(dst.label, src.label);
    EXPECT_EQ(dst.offset, src.offset);
    EXPECT_EQ(dst.position, src.position);
    EXPECT_EQ(dst.tint, src.tint);
    EXPECT_EQ(dst.mood, src.mood);
    EXPECT_EQ(dst.texture.getPath(), src.texture.getPath());
    EXPECT_EQ(dst.numbers, src.numbers);
    EXPECT_EQ(dst.points, src.points);
    EXPECT_EQ(dst.reals, src.reals);
    EXPECT_EQ(dst.precisions, src.precisions);
    EXPECT_EQ(dst.tags, src.tags);
    EXPECT_EQ(dst.flags, src.flags);
    EXPECT_EQ(dst.byName, src.byName);
    EXPECT_EQ(dst.byCode, src.byCode);
    // Copier inserts map entries by key pointer, so uint32 keys survive
    // even though JSON serialization throws them away.
    EXPECT_EQ(dst.byUnsigned, src.byUnsigned);

    EXPECT_EQ(dst.wide, 0);
    EXPECT_EQ(dst.tiny, 0);
    EXPECT_EQ(dst.orientation, glm::quat{});
    EXPECT_EQ(dst.matrix, glm::mat4(1.0f));
    EXPECT_EQ(dst.rect, script::ScriptRect{});
    EXPECT_EQ(dst.raw, nullptr);
}

TEST(ReflectionRuntimeBaseline, ScriptCrossingAndSkippedMembers)
{
    prepare();
    const script::ScriptRef ref = objectRef();

    EXPECT_EQ((*script::scriptIf<bool>(script::readField(ref, "flag"))), true);
    EXPECT_EQ((*script::scriptIf<int64_t>(script::readField(ref, "signedInt"))), 7);
    EXPECT_EQ((*script::scriptIf<int64_t>(script::readField(ref, "code"))), 9);
    EXPECT_EQ((*script::scriptIf<int64_t>(script::readField(ref, "tiny"))), 3);
    EXPECT_DOUBLE_EQ((*script::scriptIf<double>(script::readField(ref, "scalar"))), 1.5);
    EXPECT_DOUBLE_EQ((*script::scriptIf<double>(script::readField(ref, "precise"))), 2.25);
    EXPECT_EQ((*script::scriptIf<std::string>(script::readField(ref, "label"))), "hero");
    EXPECT_EQ((*script::scriptIf<glm::vec2>(script::readField(ref, "offset"))), glm::vec2(4.0f, 5.0f));
    EXPECT_EQ((*script::scriptIf<glm::vec3>(script::readField(ref, "position"))), glm::vec3(1.0f, 2.0f, 3.0f));
    EXPECT_EQ((*script::scriptIf<glm::vec4>(script::readField(ref, "tint"))), sample().tint);
    EXPECT_EQ((*script::scriptIf<int64_t>(script::readField(ref, "mood"))), static_cast<int64_t>(EBaselineMood::Angry));
    EXPECT_EQ((*script::scriptIf<script::ScriptRect>(script::readField(ref, "rect"))), sample().rect);

    // int8 used to truncate (300 -> 44). convertTo rejects the value instead.
    EXPECT_THROW(script::writeField(ref, "tiny", script::scriptMake(int64_t{300})), script::ScriptError);
    EXPECT_EQ(sample().tiny, static_cast<int8_t>(3));
    script::writeField(ref, "tiny", script::scriptMake(int64_t{4}));
    EXPECT_EQ(sample().tiny, static_cast<int8_t>(4));
    script::writeField(ref, "tiny", script::scriptMake(int64_t{3}));

    script::writeField(ref, "mood", script::scriptMake(std::string("Calm")));
    EXPECT_EQ(sample().mood, EBaselineMood::Calm);
    script::writeField(ref, "mood", script::scriptMake(int64_t{1}));
    EXPECT_EQ(sample().mood, EBaselineMood::Angry);

    EXPECT_EQ(script::findField(type_index_v<FBaseline>, "matrix"), nullptr);
    EXPECT_EQ(script::findField(type_index_v<FBaseline>, "orientation"), nullptr);
    EXPECT_EQ(script::findField(type_index_v<FBaseline>, "raw"), nullptr);
    EXPECT_EQ(script::findField(type_index_v<FBaseline>, "texture"), nullptr);
    EXPECT_EQ(script::findField(type_index_v<FBaseline>, "numbers"), nullptr);
    EXPECT_NE(script::findField(type_index_v<FBaseline>, "position"), nullptr);
    EXPECT_NE(script::findMethod(type_index_v<FBaseline>, "scaled"), nullptr);
    EXPECT_NE(script::findMethod(type_index_v<FBaseline>, "observe"), nullptr);
    EXPECT_EQ(script::findMethod(type_index_v<FBaseline>, "poke"), nullptr);

    EXPECT_TRUE(script::crossesToScripts(type_index_v<int8_t>));
    EXPECT_TRUE(script::crossesToScripts(type_index_v<uint8_t>));
    EXPECT_TRUE(script::crossesToScripts(type_index_v<int16_t>));
    EXPECT_TRUE(script::crossesToScripts(type_index_v<uint16_t>));
    EXPECT_TRUE(script::crossesToScripts(type_index_v<unsigned long>));
    EXPECT_TRUE(script::crossesToScripts(type_index_v<long long>));
    EXPECT_TRUE(script::crossesToScripts(type_index_v<script::ScriptFnHandle>));
    EXPECT_TRUE(script::crossesToScripts(type_index_v<script::ScriptSelfHandle>));
    EXPECT_FALSE(script::crossesToScripts(type_index_v<glm::quat>));
    EXPECT_FALSE(script::crossesToScripts(type_index_v<glm::mat4>));
    EXPECT_FALSE(script::crossesToScripts(type_index_v<TextureRef>));
}

TEST(ReflectionRuntimeBaseline, LuaVisibleForms)
{
    prepare();
    sol::state lua;
    lua.open_libraries(sol::lib::base);
    registerLuaScriptBindings(lua);
    lua["obj"] = LuaScriptObject{objectRef()};

    const auto read = lua.safe_script(R"(
        local p = obj.position
        local r = obj.rect
        return p.x, p.y, p.z, type(p), r.x, r.y, r.w, r.h, type(r), type(obj), obj.mood, obj.label
    )", sol::script_pass_on_error);
    ASSERT_TRUE(read.valid()) << sol::error(read).what();
    EXPECT_DOUBLE_EQ(read.get<double>(0), 1.0);
    EXPECT_DOUBLE_EQ(read.get<double>(1), 2.0);
    EXPECT_DOUBLE_EQ(read.get<double>(2), 3.0);
    EXPECT_EQ(read.get<std::string>(3), "userdata");
    EXPECT_DOUBLE_EQ(read.get<double>(4), 1.0);
    EXPECT_DOUBLE_EQ(read.get<double>(5), 2.0);
    EXPECT_DOUBLE_EQ(read.get<double>(6), 3.0);
    EXPECT_DOUBLE_EQ(read.get<double>(7), 4.0);
    EXPECT_EQ(read.get<std::string>(8), "table");
    EXPECT_EQ(read.get<std::string>(9), "userdata");
    EXPECT_EQ(read.get<int64_t>(10), static_cast<int64_t>(EBaselineMood::Angry));
    EXPECT_EQ(read.get<std::string>(11), "hero");

    const auto write = lua.safe_script(R"(
        obj.position = Vec3.new(4, 5, 6)
        obj.offset = Vec2.new(8, 9)
        obj.flag = false
        obj.label = "lua"
        obj.mood = "Calm"
        return obj:bump(2), obj.position.x
    )", sol::script_pass_on_error);
    ASSERT_TRUE(write.valid()) << sol::error(write).what();
    EXPECT_EQ(write.get<int64_t>(0), 9);
    EXPECT_DOUBLE_EQ(write.get<double>(1), 4.0);
    EXPECT_EQ(sample().position, glm::vec3(4.0f, 5.0f, 6.0f));
    EXPECT_EQ(sample().offset, glm::vec2(8.0f, 9.0f));
    EXPECT_FALSE(sample().flag);
    EXPECT_EQ(sample().label, "lua");
    EXPECT_EQ(sample().mood, EBaselineMood::Calm);
    sample().position = {1.0f, 2.0f, 3.0f};
    sample().offset   = {4.0f, 5.0f};
    sample().flag     = true;
    sample().label    = "hero";
    sample().mood     = EBaselineMood::Angry;

    for (const char* source : {"return obj.matrix", "return obj.raw", "return obj.texture", "obj.rect = {x=1, y=2, w=3, h=4}"}) {
        const auto rejected = lua.safe_script(source, sol::script_pass_on_error);
        EXPECT_FALSE(rejected.valid()) << source;
    }

    lua_State* L = lua.lua_state();
    pushLuaValue(L, script::scriptMake(glm::vec3{1.0f, 2.0f, 3.0f}));
    const script::ScriptValue back = toScriptValue(L, -1);
    EXPECT_EQ((*script::scriptIf<glm::vec3>(back)), glm::vec3(1.0f, 2.0f, 3.0f));
    lua_pop(L, 1);

    pushLuaValue(L, script::scriptMake(script::ScriptRect{1.0f, 2.0f, 3.0f, 4.0f}));
    EXPECT_EQ(lua_type(L, -1), LUA_TTABLE);
    EXPECT_THROW((void)toScriptValue(L, -1), script::ScriptError);
    lua_pop(L, 1);

    pushLuaValue(L, script::scriptMake(script::ScriptFnHandle{}));
    EXPECT_EQ(lua_type(L, -1), LUA_TNIL);
    lua_pop(L, 1);
    pushLuaValue(L, script::scriptMake(script::ScriptFnHandle{42}));
    EXPECT_EQ(lua_type(L, -1), LUA_TNIL);
    lua_pop(L, 1);
    pushLuaValue(L, script::scriptMake(script::ScriptSelfHandle{}));
    EXPECT_EQ(lua_type(L, -1), LUA_TNIL);
    lua_pop(L, 1);
}

TEST(ReflectionRuntimeBaseline, ReferenceArgumentsWriteThrough)
{
    prepare();
    FBaseline& obj = sample();
    Class*     cls = ClassRegistry::instance().getClass(type_index_v<FBaseline>);
    ASSERT_NE(cls, nullptr);

    int written = 0;
    cls->invoke<void>("writeBack", &obj, written);
    EXPECT_EQ(written, obj.signedInt);

    cls->invoke<void>("observe", &obj, obj.signedInt);
    EXPECT_FALSE(obj.boundCopy);
    EXPECT_EQ(obj.lastSeen, obj.signedInt);

    const int other = 11;
    cls->invoke<void>("observe", &obj, other);
    EXPECT_TRUE(obj.boundCopy);
    EXPECT_EQ(obj.lastSeen, other);

    int storage = 3;
    cls->invoke<void>("poke", &obj, &storage);
    EXPECT_EQ(storage, 9);

    const int frozen = 1;
    EXPECT_THROW(cls->invoke<void>("writeBack", &obj, frozen), std::runtime_error);

    refl::MetaValue factor = refl::MetaValue::make(2.0f);
    const refl::MetaValue scaled = cls->functions.at("scaled").invoker(&obj, std::span<refl::MetaValue>(&factor, 1));
    ASSERT_NE(scaled.tryGet<float>(), nullptr);
    EXPECT_FLOAT_EQ(*scaled.tryGet<float>(), obj.scalar * 2.0f);
}

TEST(ReflectionRuntimeBaseline, EditorGraphAndMetadata)
{
    prepare();
    FBaseline& obj = sample();
    const PropertyGraph graph = PropertyGraph::build(type_index_v<FBaseline>, {&obj});

    const auto nodeOf = [&](const char* name) -> const PropertyNode* {
        const PropertyNode* node = graph.find(name);
        if (!node) {
            std::string known;
            for (const PropertyNode& item : graph.getNodes()) {
                if (item.name.find("by") != std::string::npos) {
                    known += item.name;
                    known += "\n";
                }
            }
            ADD_FAILURE() << "missing " << name << "\n" << known;
        }
        return node;
    };
    const auto expectType = [&](const char* name, type_index_t type) {
        if (const PropertyNode* node = nodeOf(name)) {
            EXPECT_EQ(node->valueType, type) << name;
        }
    };

    expectType("flag", type_index_v<bool>);
    expectType("signedInt", type_index_v<int>);
    expectType("code", type_index_v<uint32_t>);
    expectType("scalar", type_index_v<float>);
    expectType("precise", type_index_v<double>);
    expectType("label", type_index_v<std::string>);
    expectType("offset", type_index_v<glm::vec2>);
    expectType("position", type_index_v<glm::vec3>);
    expectType("tint", type_index_v<glm::vec4>);
    if (const PropertyNode* tint = nodeOf("tint")) {
        EXPECT_TRUE(tint->bColor);
    }
    if (const PropertyNode* position = nodeOf("position")) {
        EXPECT_FALSE(position->bColor);
    }
    expectType("mood", type_index_v<EBaselineMood>);
    expectType("texture", type_index_v<TextureRef>);
    expectType("wide", type_index_v<int64_t>);
    expectType("orientation", type_index_v<glm::quat>);
    expectType("matrix", type_index_v<glm::mat4>);
    if (const PropertyNode* numbers = nodeOf("numbers")) {
        EXPECT_EQ(numbers->kind, PropertyNode::Kind::Sequence);
    }
    expectType("numbers[0]", type_index_v<int>);
    if (const PropertyNode* byName = nodeOf("byName")) {
        EXPECT_EQ(byName->kind, PropertyNode::Kind::Map);
    }
    expectType("byName[\"hp\"]", type_index_v<float>);
    expectType("byCode[4]", type_index_v<int>);
    // The header is emitted, then value lookup rejects uint32 keys (getValuePtr
    // only parses int / string / long / floating keys), so the row is dropped.
    if (const PropertyNode* header = nodeOf("byUnsigned")) {
        EXPECT_EQ(header->kind, PropertyNode::Kind::Map);
    }
    EXPECT_EQ(graph.find("byUnsigned[8]"), nullptr);
    EXPECT_TRUE(graph.hasRetainedEditors());

    obj.scalar = 1.5f;
    const std::string scalarError = PropertyEditorMetadata::validationError(slotOf("scalar"), &obj);
    EXPECT_FALSE(scalarError.empty());

    Class* cls = ClassRegistry::instance().getClass(type_index_v<FBaseline>);
    cls->properties.at("position").metadata.set(
        reflection::Meta::ManipulateSpec::name,
        reflection::Meta::ManipulateSpec{
            .type = reflection::Meta::ManipulateSpec::Slider,
            .min  = 0.0f,
            .max  = 1.0f,
            .step = 0.1f,
        });
    const std::string vectorError = PropertyEditorMetadata::validationError(slotOf("position"), &obj);
    EXPECT_NE(vectorError.find("Component must be between"), std::string::npos);

    // Widget construction (EditorAutoPropertySection::buildEditor) is private
    // and mounts retained controls. The type switch matches isLeafValueType
    // plus the color flag asserted above.
}

TEST(ReflectionRuntimeBaseline, ComponentJsonCustomScriptPayload)
{
    prepare();
    LuaScriptComponent component;
    component.addScript("Content/Scripts/hero.lua");
    component.scripts[0].enabled = false;
    component.scripts[0].executionOrderOverride = 2;
    component.scripts[0].propertyOverrides["tint"] = script::scriptMake(glm::vec3{1.0f, 0.0f, 0.5f});

    const auto* ops = ECSRegistry::get().getComponentOps(type_index_v<LuaScriptComponent>);
    ASSERT_NE(ops, nullptr);
    const nlohmann::json json = assembleComponentJson(&component,
                                                      type_index_v<LuaScriptComponent>,
                                                      "LuaScriptComponent",
                                                      ops,
                                                      nullptr);
    EXPECT_EQ(json.dump(),
              R"({"scripts":[{"enabled":false,"executionOrder":2,"propertyOverrides":{"tint":[1.0,0.0,0.5]},"scriptPath":"Content/Scripts/hero.lua"}]})");

    LuaScriptComponent restored;
    ops->deserializeCustom(&restored, json);
    ASSERT_EQ(restored.scripts.size(), 1u);
    EXPECT_EQ(restored.scripts[0].scriptPath, "Content/Scripts/hero.lua");
    EXPECT_FALSE(restored.scripts[0].enabled);
    ASSERT_TRUE(restored.scripts[0].executionOrderOverride.has_value());
    EXPECT_EQ(*restored.scripts[0].executionOrderOverride, 2);
    EXPECT_EQ((*script::scriptIf<glm::vec3>(restored.scripts[0].propertyOverrides.at("tint"))), glm::vec3(1.0f, 0.0f, 0.5f));
}

} // namespace ya::rr0
