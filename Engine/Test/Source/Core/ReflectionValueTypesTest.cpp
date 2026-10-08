#include "Core/Math/GLM.h"
#include "Core/Reflection/CoreValueTypes.h"
#include "Core/Reflection/ReflectionSerializer.h"
#include "Core/Reflection/ValueFacets.h"
#include "Core/Scripting/ScriptValue.h"

#include <gtest/gtest.h>

#include <cstdint>
#include <limits>
#include <string>
#include <utility>

namespace
{

struct HookGuard
{
    HookGuard() = default;
    ~HookGuard() { refl::TypeRegistry::instance().missingFacetHook = nullptr; }
};

template <class T>
const refl::TypeOps* expectRegistered(const char* name)
{
    refl::TypeRegistry& registry = refl::TypeRegistry::instance();
    const refl::TypeOps* ops     = registry.find(ya::type_index_v<T>);
    EXPECT_NE(ops, nullptr) << name;
    if (ops == nullptr) {
        return nullptr;
    }
    EXPECT_EQ(ops->name, name);
    EXPECT_EQ(ops->kind, refl::EValueKind::Object);
    EXPECT_EQ(ops->life.size, sizeof(T));
    EXPECT_EQ(registry.findByName(name), ops);
    return ops;
}

template <class T>
void expectStorage(refl::EMetaStorage storage)
{
    const T            value{};
    const refl::MetaValue made = refl::MetaValue::make(value);
    EXPECT_EQ(made.storage(), storage) << refl::detail::compilerTypeName<T>();
    EXPECT_EQ(refl::metaValueFitsInline<T>(), storage == refl::EMetaStorage::Inline);
}

template <class T>
void expectRoundTrip(const T& value)
{
    const refl::MetaValue made = refl::MetaValue::make(value);
    const T* got               = made.tryGet<T>();
    ASSERT_NE(got, nullptr);
    EXPECT_EQ(*got, value);

    const refl::MetaValue copied = made;
    const T* copiedValue         = copied.tryGet<T>();
    ASSERT_NE(copiedValue, nullptr);
    EXPECT_EQ(*copiedValue, value);

    refl::MetaValue movedFrom = refl::MetaValue::make(value);
    const refl::MetaValue moved{std::move(movedFrom)};
    const T* movedValue = moved.tryGet<T>();
    ASSERT_NE(movedValue, nullptr);
    EXPECT_EQ(*movedValue, value);
    EXPECT_TRUE(movedFrom.isNull());
}

const ya::reflection::FEqualFacet* equalFacet(refl::type_index_t type)
{
    return refl::TypeRegistry::instance().resolveFacet<ya::reflection::FEqualFacet>(type);
}

const ya::reflection::FPrintFacet* printFacet(refl::type_index_t type)
{
    return refl::TypeRegistry::instance().resolveFacet<ya::reflection::FPrintFacet>(type);
}

} // namespace

TEST(CoreValueTypes, RegisteredNamesKindsAndSizes)
{
    ya::reflection::registerCoreValueTypes();

    expectRegistered<glm::vec2>("glm::vec2");
    expectRegistered<glm::vec3>("glm::vec3");
    expectRegistered<glm::vec4>("glm::vec4");
    expectRegistered<glm::quat>("glm::quat");
    expectRegistered<glm::mat3>("glm::mat3");
    expectRegistered<glm::mat4>("glm::mat4");
    expectRegistered<ya::script::ScriptRect>("script::ScriptRect");
    expectRegistered<ya::script::ScriptRef>("script::ScriptRef");
    expectRegistered<ya::script::ScriptFnHandle>("script::ScriptFnHandle");
    expectRegistered<ya::script::ScriptSelfHandle>("script::ScriptSelfHandle");

    EXPECT_EQ(sizeof(glm::vec2), 8u);
    EXPECT_EQ(sizeof(glm::vec3), 12u);
    EXPECT_EQ(sizeof(glm::vec4), 16u);
    EXPECT_EQ(sizeof(glm::quat), 16u);
    EXPECT_EQ(sizeof(glm::mat3), 36u);
    EXPECT_EQ(sizeof(glm::mat4), 64u);
    EXPECT_EQ(sizeof(ya::script::ScriptRect), 16u);
    // type (8) + kind (4) + pad (4) + a (8) + b (8). Still within the 32-byte inline cap.
    EXPECT_EQ(sizeof(ya::script::ScriptRef), 32u);
    EXPECT_EQ(sizeof(ya::script::ScriptFnHandle), 8u);
    EXPECT_EQ(sizeof(ya::script::ScriptSelfHandle), 8u);
}

TEST(CoreValueTypes, StorageInlineAndBoxed)
{
    expectStorage<glm::vec2>(refl::EMetaStorage::Inline);
    expectStorage<glm::vec3>(refl::EMetaStorage::Inline);
    expectStorage<glm::vec4>(refl::EMetaStorage::Inline);
    expectStorage<glm::quat>(refl::EMetaStorage::Inline);
    expectStorage<ya::script::ScriptRect>(refl::EMetaStorage::Inline);
    expectStorage<ya::script::ScriptRef>(refl::EMetaStorage::Inline);
    expectStorage<ya::script::ScriptFnHandle>(refl::EMetaStorage::Inline);
    expectStorage<ya::script::ScriptSelfHandle>(refl::EMetaStorage::Inline);
    expectStorage<glm::mat3>(refl::EMetaStorage::Boxed);
    expectStorage<glm::mat4>(refl::EMetaStorage::Boxed);
}

TEST(CoreValueTypes, RoundTripCopyAndMove)
{
    expectRoundTrip(glm::vec2{1.0f, 2.0f});
    expectRoundTrip(glm::vec3{1.0f, 2.0f, 3.0f});
    expectRoundTrip(glm::vec4{1.0f, 2.0f, 3.0f, 4.0f});
    glm::quat rotation;
    rotation.x = 0.0f;
    rotation.y = 0.0f;
    rotation.z = 0.0f;
    rotation.w = 1.0f;
    expectRoundTrip(rotation);
    expectRoundTrip(glm::mat3{1.0f});
    expectRoundTrip(glm::mat4{1.0f});

    ya::script::ScriptRect rect{1.0f, 2.0f, 3.0f, 4.0f};
    expectRoundTrip(rect);

    ya::script::ScriptRef ref;
    ref.type = 11;
    ref.kind = 2;
    ref.a    = 3;
    ref.b    = 4;
    expectRoundTrip(ref);

    expectRoundTrip(ya::script::ScriptFnHandle{7});
    expectRoundTrip(ya::script::ScriptSelfHandle{8});
}

TEST(CoreValueTypes, BoxedMat4CopyIsIndependent)
{
    const glm::mat4 original{1.0f};
    refl::MetaValue made = refl::MetaValue::make(original);
    ASSERT_EQ(made.storage(), refl::EMetaStorage::Boxed);

    refl::MetaValue copied = made;
    glm::mat4* left        = made.tryGet<glm::mat4>();
    glm::mat4* right       = copied.tryGet<glm::mat4>();
    ASSERT_NE(left, nullptr);
    ASSERT_NE(right, nullptr);
    EXPECT_NE(left, right);
    EXPECT_EQ(*left, *right);

    (*left)[0][0] = 7.0f;
    EXPECT_EQ((*right)[0][0], 1.0f);
}

TEST(CoreValueTypes, RefWriteLandsOnOriginal)
{
    glm::vec3 original{1.0f, 2.0f, 3.0f};
    refl::MetaValue borrowed = refl::MetaValue::ref(original);
    EXPECT_EQ(borrowed.storage(), refl::EMetaStorage::Ref);

    glm::vec3* view = borrowed.tryGet<glm::vec3>();
    ASSERT_NE(view, nullptr);
    EXPECT_EQ(view, &original);
    view->y = 9.0f;
    EXPECT_EQ(original.y, 9.0f);
}

TEST(CoreValueTypes, EqualFacet)
{
    const glm::vec3 left{1.0f, 2.0f, 3.0f};
    const glm::vec3 same{1.0f, 2.0f, 3.0f};
    const glm::vec3 other{1.0f, 2.0f, 4.0f};
    const ya::reflection::FEqualFacet* vec3 = equalFacet(ya::type_index_v<glm::vec3>);
    ASSERT_NE(vec3, nullptr);
    EXPECT_TRUE(vec3->equals(&left, &same));
    EXPECT_FALSE(vec3->equals(&left, &other));

    const glm::vec2 nanLeft{std::numeric_limits<float>::quiet_NaN(), 1.0f};
    const glm::vec2 nanRight = nanLeft;
    const ya::reflection::FEqualFacet* vec2 = equalFacet(ya::type_index_v<glm::vec2>);
    ASSERT_NE(vec2, nullptr);
    EXPECT_FALSE(vec2->equals(&nanLeft, &nanRight));

    ya::script::ScriptRef refA;
    refA.kind = 1;
    refA.a    = 2;
    ya::script::ScriptRef refB = refA;
    ya::script::ScriptRef refC = refA;
    refC.b                     = 1;
    const ya::reflection::FEqualFacet* refEq = equalFacet(ya::type_index_v<ya::script::ScriptRef>);
    ASSERT_NE(refEq, nullptr);
    EXPECT_TRUE(refEq->equals(&refA, &refB));
    EXPECT_FALSE(refEq->equals(&refA, &refC));

    const ya::script::ScriptFnHandle fnA{3};
    const ya::script::ScriptFnHandle fnB{3};
    const ya::script::ScriptFnHandle fnC{4};
    const ya::reflection::FEqualFacet* fnEq = equalFacet(ya::type_index_v<ya::script::ScriptFnHandle>);
    ASSERT_NE(fnEq, nullptr);
    EXPECT_TRUE(fnEq->equals(&fnA, &fnB));
    EXPECT_FALSE(fnEq->equals(&fnA, &fnC));
}

TEST(CoreValueTypes, PrintFacet)
{
    const glm::vec3 vec{1.0f, 2.0f, 3.0f};
    const refl::TypeOps* vecOps = refl::TypeRegistry::instance().find(ya::type_index_v<glm::vec3>);
    ASSERT_NE(vecOps, nullptr);
    const ya::reflection::FPrintFacet* vecPrint = printFacet(vecOps->type);
    ASSERT_NE(vecPrint, nullptr);
    EXPECT_EQ(vecPrint->print(*vecOps, &vec), "(1, 2, 3)");

    glm::quat rotation;
    rotation.x = 1.0f;
    rotation.y = 2.0f;
    rotation.z = 3.0f;
    rotation.w = 4.0f;
    const refl::TypeOps* quatOps = refl::TypeRegistry::instance().find(ya::type_index_v<glm::quat>);
    ASSERT_NE(quatOps, nullptr);
    EXPECT_EQ(printFacet(quatOps->type)->print(*quatOps, &rotation), "(1, 2, 3, 4)");

    const glm::mat3 matrix{1.0f};
    const refl::TypeOps* matOps = refl::TypeRegistry::instance().find(ya::type_index_v<glm::mat3>);
    ASSERT_NE(matOps, nullptr);
    EXPECT_EQ(printFacet(matOps->type)->print(*matOps, &matrix), "((1, 0, 0), (0, 1, 0), (0, 0, 1))");

    const ya::script::ScriptRect rect{1.0f, 2.0f, 3.0f, 4.0f};
    const refl::TypeOps* rectOps = refl::TypeRegistry::instance().find(ya::type_index_v<ya::script::ScriptRect>);
    ASSERT_NE(rectOps, nullptr);
    EXPECT_EQ(printFacet(rectOps->type)->print(*rectOps, &rect), "(1, 2, 3, 4)");

    ya::script::ScriptRef ref;
    ref.kind = 2;
    ref.a    = 3;
    ref.b    = 4;
    const refl::TypeOps* refOps = refl::TypeRegistry::instance().find(ya::type_index_v<ya::script::ScriptRef>);
    ASSERT_NE(refOps, nullptr);
    EXPECT_EQ(printFacet(refOps->type)->print(*refOps, &ref), "ref(2, 3, 4)");

    const ya::script::ScriptFnHandle fn{7};
    const refl::TypeOps* fnOps = refl::TypeRegistry::instance().find(ya::type_index_v<ya::script::ScriptFnHandle>);
    ASSERT_NE(fnOps, nullptr);
    EXPECT_EQ(printFacet(fnOps->type)->print(*fnOps, &fn), "fn(7)");

    const ya::script::ScriptSelfHandle self{8};
    const refl::TypeOps* selfOps = refl::TypeRegistry::instance().find(ya::type_index_v<ya::script::ScriptSelfHandle>);
    ASSERT_NE(selfOps, nullptr);
    EXPECT_EQ(printFacet(selfOps->type)->print(*selfOps, &self), "self(8)");
}

TEST(CoreValueTypes, MissingPrintUsesFallback)
{
    struct NoPrint
    {
        int value = 1;
    };

    refl::TypeRegistry& registry = refl::TypeRegistry::instance();
    registry.registerType<NoPrint>("RR2NoPrint");
    const refl::TypeOps* ops = registry.find(ya::type_index_v<NoPrint>);
    ASSERT_NE(ops, nullptr);

    const ya::reflection::FPrintFacet* facet = printFacet(ops->type);
    ASSERT_NE(facet, nullptr);
    EXPECT_EQ(facet, &ya::reflection::FPrintFacet::fallback());

    const NoPrint value{};
    EXPECT_EQ(facet->print(*ops, &value), "<RR2NoPrint>");
}

TEST(CoreValueTypes, MissingEqualIsNullAndSkipsHook)
{
    struct NoEqual
    {
        int value = 0;
    };

    HookGuard guard;
    int       calls = 0;
    refl::TypeRegistry& registry = refl::TypeRegistry::instance();
    registry.registerType<NoEqual>("RR2NoEqual");
    registry.missingFacetHook = [&calls](refl::type_index_t, std::string_view) { ++calls; };

    EXPECT_EQ(equalFacet(ya::type_index_v<NoEqual>), nullptr);
    EXPECT_EQ(calls, 0);
}

TEST(CoreValueTypes, RegisterIsIdempotent)
{
    refl::TypeRegistry& registry = refl::TypeRegistry::instance();
    const refl::TypeOps* before  = registry.find(ya::type_index_v<glm::vec3>);
    ASSERT_NE(before, nullptr);

    EXPECT_NO_THROW(ya::reflection::registerCoreValueTypes());
    EXPECT_NO_THROW(ya::reflection::registerCoreValueTypes());

    EXPECT_EQ(registry.find(ya::type_index_v<glm::vec3>), before);
    EXPECT_EQ(registry.findByName("glm::vec3"), before);
    EXPECT_EQ(before->name, "glm::vec3");
}

TEST(CoreValueTypes, Vec3SharesClassRow)
{
    ya::reflection::registerCoreValueTypes();

    Class* cls = ClassRegistry::instance().getClass("glm::vec3");
    ASSERT_NE(cls, nullptr);
    EXPECT_EQ(cls->name, "glm::vec3");
    EXPECT_EQ(cls->typeIndex, ya::type_index_v<glm::vec3>);
    EXPECT_NE(cls->getProperty("x"), nullptr);
    EXPECT_NE(cls->getProperty("y"), nullptr);
    EXPECT_NE(cls->getProperty("z"), nullptr);

    const refl::TypeOps* ops = refl::TypeRegistry::instance().findByName("glm::vec3");
    ASSERT_NE(ops, nullptr);
    EXPECT_EQ(ops->type, ya::type_index_v<glm::vec3>);
    EXPECT_EQ(ops->kind, refl::EValueKind::Object);
    EXPECT_EQ(ClassRegistry::instance().getClass(ya::type_index_v<glm::vec3>), cls);

    auto again = ClassRegistry::instance().registerClass<glm::vec3>("glm::vec3", new Class("glm::vec3"));
    EXPECT_EQ(again.get(), cls);
    EXPECT_EQ(refl::TypeRegistry::instance().find(ya::type_index_v<glm::vec3>), ops);
    EXPECT_NE(cls->getProperty("x"), nullptr);
}

TEST(CoreValueTypes, ClassFirstAndTypeFirstShareOneRow)
{
    struct ClassFirst
    {
        int value = 0;
    };
    struct TypeFirst
    {
        int value = 0;
    };

    auto cls = ClassRegistry::instance().registerClass<ClassFirst>("RR2ClassFirst", new Class("RR2ClassFirst"));
    refl::TypeOps& ops = refl::TypeRegistry::instance().registerType<ClassFirst>("RR2ClassFirst");
    EXPECT_EQ(ops.name, "RR2ClassFirst");
    EXPECT_EQ(ops.kind, refl::EValueKind::Object);
    EXPECT_EQ(&ops, refl::TypeRegistry::instance().findByName("RR2ClassFirst"));
    EXPECT_EQ(ClassRegistry::instance().getClass(ya::type_index_v<ClassFirst>), cls.get());

    refl::TypeOps& opsFirst = refl::TypeRegistry::instance().registerType<TypeFirst>("RR2TypeFirst");
    auto clsAfter = ClassRegistry::instance().registerClass<TypeFirst>("RR2TypeFirst", new Class("RR2TypeFirst"));
    EXPECT_EQ(clsAfter->name, "RR2TypeFirst");
    EXPECT_EQ(&opsFirst, refl::TypeRegistry::instance().find(ya::type_index_v<TypeFirst>));
    EXPECT_EQ(refl::TypeRegistry::instance().findByName("RR2TypeFirst"), &opsFirst);
}

TEST(CoreValueTypes, JsonFacetRoundTripsWithoutATypeSwitch)
{
    struct JsonOnly
    {
        int n = 0;
    };

    ya::reflection::registerCoreValueTypes();
    refl::TypeRegistry& registry = refl::TypeRegistry::instance();
    registry.registerType<JsonOnly>("RR3JsonOnly");
    registry.setFacet<ya::reflection::FJsonFacet>(
        ya::type_index_v<JsonOnly>,
        ya::reflection::FJsonFacet{
            .serialize = [](const void* value) {
                return nlohmann::json{{"n", static_cast<const JsonOnly*>(value)->n}};
            },
            .deserialize = [](void* value, const nlohmann::json& json) {
                static_cast<JsonOnly*>(value)->n = json.at("n").get<int>();
            },
        });

    JsonOnly value{.n = 7};
    const nlohmann::json json = ya::ReflectionSerializer::serializeByRuntimeReflection(
        &value, ya::type_index_v<JsonOnly>, "RR3JsonOnly");
    EXPECT_EQ(json.dump(), R"({"n":7})");

    JsonOnly restored;
    ya::ReflectionSerializer::deserializeByRuntimeReflection(
        &restored, ya::type_index_v<JsonOnly>, json, "RR3JsonOnly");
    EXPECT_EQ(restored.n, 7);

    const glm::vec3 vec{1.0f, 2.0f, 3.0f};
    const nlohmann::json vecJson = ya::ReflectionSerializer::serializeByRuntimeReflection(
        &vec, ya::type_index_v<glm::vec3>, "glm::vec3");
    ASSERT_TRUE(vecJson.is_array());
    ASSERT_EQ(vecJson.size(), 3u);
    EXPECT_FLOAT_EQ(vecJson[0].get<float>(), 1.0f);
    EXPECT_FLOAT_EQ(vecJson[1].get<float>(), 2.0f);
    EXPECT_FLOAT_EQ(vecJson[2].get<float>(), 3.0f);

    glm::vec3 vecRestored{};
    ya::ReflectionSerializer::deserializeByRuntimeReflection(
        &vecRestored, ya::type_index_v<glm::vec3>, nlohmann::json{{"x", 4.0f}, {"y", 5.0f}, {"z", 6.0f}}, "glm::vec3");
    EXPECT_FLOAT_EQ(vecRestored.x, 4.0f);
    EXPECT_FLOAT_EQ(vecRestored.y, 5.0f);
    EXPECT_FLOAT_EQ(vecRestored.z, 6.0f);

    const auto* equal = registry.facet<ya::reflection::FEqualFacet>(ya::type_index_v<int>);
    ASSERT_NE(equal, nullptr);
    ASSERT_NE(equal->equals, nullptr);
    const int left = 3;
    const int right = 3;
    const int other = 4;
    EXPECT_TRUE(equal->equals(&left, &right));
    EXPECT_FALSE(equal->equals(&left, &other));
}
