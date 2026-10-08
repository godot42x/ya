#include "reflects-core/lib.h"

#include <gtest/gtest.h>

namespace
{

struct ModuleOwned
{
    int n = 0;
};

} // namespace

TEST(RegistryModule, UnregisterDropsOwnedClassAndKeepsBuiltin)
{
    auto          &registry = ClassRegistry::instance();
    const uint64_t before   = registry.version();
    int            notes    = 0;
    const uint64_t listener = registry.addListener([&](uint64_t) { ++notes; });

    const auto module = registry.allocateModule();
    registry.setRegistrationModule(module);
    Register<ModuleOwned>("ModuleOwned").property("n", &ModuleOwned::n);
    registry.setRegistrationModule(kBuiltinModule);

    EXPECT_NE(registry.getClass("ModuleOwned"), nullptr);
    EXPECT_GT(notes, 0);
    EXPECT_GT(registry.version(), before);

    Class *probe = registry.getClass("CallProbe");
    ASSERT_NE(probe, nullptr);
    const uint64_t mid = registry.version();
    registry.setRegistrationModule(module);
    Function extra;
    extra.name     = "fromModule";
    extra.argCount = 0;
    probe->insertFunction(std::move(extra));
    registry.setRegistrationModule(kBuiltinModule);
    EXPECT_TRUE(probe->hasFunction("fromModule"));
    EXPECT_GT(registry.version(), mid);

    const uint64_t duplicate = registry.version();
    Function       again;
    again.name = "fromModule";
    probe->insertFunction(std::move(again));
    EXPECT_EQ(registry.version(), duplicate);

    registry.unregisterModule(module);
    EXPECT_EQ(registry.getClass("ModuleOwned"), nullptr);
    EXPECT_EQ(refl::TypeRegistry::instance().findByName("ModuleOwned"), nullptr);
    EXPECT_FALSE(probe->hasFunction("fromModule"));
    EXPECT_TRUE(probe->hasFunction("observe"));

    const uint64_t frozen = registry.version();
    registry.unregisterModule(kBuiltinModule);
    EXPECT_EQ(registry.version(), frozen);
    EXPECT_NE(registry.getClass("CallProbe"), nullptr);
    registry.removeListener(listener);
}
