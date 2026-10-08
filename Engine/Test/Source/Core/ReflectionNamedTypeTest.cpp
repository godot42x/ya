#include "Core/Reflection/DeferredInitializer.h"
#include "Core/Reflection/Reflection.h"
#include "Core/Reflection/ReflectionSerializer.h"
#include "ECS/Component/Material/PBRMaterialComponent.h"
#include "ECS/Component/Material/PhongMaterialComponent.h"
#include "ECS/Component/Material/UnlitMaterialComponent.h"

#include <gtest/gtest.h>

#include <string>

namespace
{

struct NamedReflectBase
{
    YA_REFLECT_BEGIN(NamedReflectBase)
    YA_REFLECT_FIELD(baseValue)
    YA_REFLECT_END()

    int baseValue = 0;
};

struct NamedReflectOwner
{
    struct Plain
    {
        YA_REFLECT_BEGIN_NAMED(Plain, "NamedReflectOwner::Plain")
        YA_REFLECT_FIELD(value)
        YA_REFLECT_END()

        int value = 0;
    };

    struct Derived : NamedReflectBase
    {
        YA_REFLECT_BEGIN_NAMED(Derived, "NamedReflectOwner::Derived", NamedReflectBase)
        YA_REFLECT_FIELD(extra)
        YA_REFLECT_END()

        int extra = 0;
    };
};

void ensureNamedReflection()
{
    NamedReflectBase::__ensure_reflection_registered();
    NamedReflectOwner::Plain::__ensure_reflection_registered();
    NamedReflectOwner::Derived::__ensure_reflection_registered();
    ya::PhongMaterialComponent::AuthoringParams::__ensure_reflection_registered();
    ya::UnlitMaterialComponent::AuthoringParams::__ensure_reflection_registered();
    ya::PBRMaterialComponent::AuthoringParams::__ensure_reflection_registered();
    ya::reflection::DeferredInitializerQueue::instance().executeAll();
}

} // namespace

TEST(ReflectionNamedType, ExplicitNameRegistersAndSerializes)
{
    ensureNamedReflection();

    Class *plain = ClassRegistry::instance().getClass("NamedReflectOwner::Plain");
    ASSERT_NE(plain, nullptr);
    EXPECT_EQ(plain->name, "NamedReflectOwner::Plain");
    EXPECT_NE(plain->getProperty("value"), nullptr);
    const refl::TypeOps *plainOps = refl::TypeRegistry::instance().findByName("NamedReflectOwner::Plain");
    ASSERT_NE(plainOps, nullptr);
    EXPECT_EQ(plainOps->type, ya::type_index_v<NamedReflectOwner::Plain>);

    NamedReflectOwner::Plain src;
    src.value = 7;
    const nlohmann::json json = ya::ReflectionSerializer::serializeByRuntimeReflection(src);
    EXPECT_EQ(json["value"], 7);
    NamedReflectOwner::Plain restored;
    ya::ReflectionSerializer::deserializeByRuntimeReflection(restored, json, "NamedReflectOwner::Plain");
    EXPECT_EQ(restored.value, 7);

    Class *derived = ClassRegistry::instance().getClass("NamedReflectOwner::Derived");
    ASSERT_NE(derived, nullptr);
    EXPECT_EQ(derived->name, "NamedReflectOwner::Derived");
    EXPECT_NE(derived->getProperty("extra"), nullptr);
    const refl::TypeOps *derivedOps = refl::TypeRegistry::instance().findByName("NamedReflectOwner::Derived");
    ASSERT_NE(derivedOps, nullptr);
    EXPECT_EQ(derivedOps->type, ya::type_index_v<NamedReflectOwner::Derived>);

    NamedReflectOwner::Derived child;
    child.baseValue = 3;
    child.extra     = 9;
    const nlohmann::json derivedJson = ya::ReflectionSerializer::serializeByRuntimeReflection(child);
    EXPECT_EQ(derivedJson["extra"], 9);
    ASSERT_TRUE(derivedJson.contains("__base__"));
    EXPECT_EQ(derivedJson["__base__"]["NamedReflectBase"]["baseValue"], 3);

    NamedReflectOwner::Derived derivedRestored;
    ya::ReflectionSerializer::deserializeByRuntimeReflection(derivedRestored, derivedJson, "NamedReflectOwner::Derived");
    EXPECT_EQ(derivedRestored.extra, 9);
    EXPECT_EQ(derivedRestored.baseValue, 3);
}

TEST(ReflectionNamedType, MaterialAuthoringParamsUseExplicitNames)
{
    ensureNamedReflection();

    struct Row
    {
        const char       *name;
        refl::type_index_t type;
    };
    const Row rows[] = {
        {"PhongMaterialComponent::AuthoringParams", ya::type_index_v<ya::PhongMaterialComponent::AuthoringParams>},
        {"UnlitMaterialComponent::AuthoringParams", ya::type_index_v<ya::UnlitMaterialComponent::AuthoringParams>},
        {"PBRMaterialComponent::AuthoringParams", ya::type_index_v<ya::PBRMaterialComponent::AuthoringParams>},
    };
    for (const Row &row : rows) {
        Class *cls = ClassRegistry::instance().getClass(row.name);
        ASSERT_NE(cls, nullptr) << row.name;
        EXPECT_EQ(cls->name, row.name);
        const refl::TypeOps *ops = refl::TypeRegistry::instance().findByName(row.name);
        ASSERT_NE(ops, nullptr) << row.name;
        EXPECT_EQ(ops->type, row.type) << row.name;
    }
    EXPECT_EQ(ClassRegistry::instance().getClass("AuthoringParams"), nullptr);
}
