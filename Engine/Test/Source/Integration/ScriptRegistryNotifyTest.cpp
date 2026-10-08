#include "Core/Scripting/ScriptBindings.h"
#include "GameEditor/Inspector/ReflectionCache.h"

#include <reflects-core/lib.h>

#include <gtest/gtest.h>

namespace
{

struct NotifyProbe
{
    int value = 1;

    int bump(int x) const { return value + x; }
};

void ensureProbe()
{
    static const bool once = [] {
        Register<NotifyProbe>("NotifyProbe").property("value", &NotifyProbe::value);
        return true;
    }();
    (void)once;
}

} // namespace

TEST(RegistryNotify, ScriptAndEditorSeeModuleUnregister)
{
    ensureProbe();
    const auto type = ya::type_index_v<NotifyProbe>;
    Class     *cls  = ClassRegistry::instance().getClass(type);
    ASSERT_NE(cls, nullptr);

    ya::ReflectionCache *cache = ya::getOrCreateReflectionCache(type);
    ASSERT_NE(cache, nullptr);
    const size_t before = cache->propertyCount;

    const auto module = ClassRegistry::instance().allocateModule();
    ClassRegistry::instance().setRegistrationModule(module);
    ya::script::addMethod(type, "bump", ya::script::fn([](const NotifyProbe &probe, int x) { return probe.bump(x); }));
    Property extra;
    extra.name      = "extra";
    extra.typeIndex = ya::type_index_v<int>;
    cls->insertProperty("extra", std::move(extra));
    ClassRegistry::instance().setRegistrationModule(kBuiltinModule);

    EXPECT_NE(ya::script::findMethod(type, "bump"), nullptr);
    EXPECT_EQ(cache->propertyCount, before + 1u);
    EXPECT_NE(cache->propertyContexts.find("extra"), cache->propertyContexts.end());

    ClassRegistry::instance().unregisterModule(module);

    EXPECT_EQ(ya::script::findMethod(type, "bump"), nullptr);
    EXPECT_EQ(cache->propertyCount, before);
    EXPECT_EQ(cache->propertyContexts.find("extra"), cache->propertyContexts.end());
    EXPECT_TRUE(cls->properties().contains("value"));
}
