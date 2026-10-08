// RR8: the migrated natives are ordinary ClassRegistry functions. The string
// registration path that remains is ScriptMethodResolver (entity getX / hasX).

#include "Core/Reflection/Reflection.h"
#include "Core/Scripting/ScriptBindings.h"
#include "ECS/Entity.h"
#include "GUI/Widgets/UIElement.h"
#include "GameRuntime/Script/GameplayScriptFunctions.h"
#include "Scene2D/TilemapComponent.h"

#include <gtest/gtest.h>

#include <string>

TEST(ScriptRuntimeNative, RegistryListsMigratedMethods)
{
    ya::registerRuntimeScriptNatives();

    Class* element = ClassRegistry::instance().getClass(ya::type_index_v<ya::UIElement>);
    ASSERT_NE(element, nullptr);
    ASSERT_TRUE(element->functions().contains("find"));
    EXPECT_TRUE(element->functions().at("find").bVariadic);
    EXPECT_EQ(element->functions().at("find").args.size(), 1u);
    EXPECT_EQ(element->functions().at("find").args[0].type, ya::type_index_v<std::string>);
    EXPECT_TRUE(element->functions().contains("destroy"));
    EXPECT_FALSE(element->functions().at("destroy").bVariadic);
    EXPECT_TRUE(element->functions().contains("layout"));
    EXPECT_TRUE(element->functions().contains("rect"));

    Class* map = ClassRegistry::instance().getClass(ya::type_index_v<ya::TilemapComponent>);
    ASSERT_NE(map, nullptr);
    const Function& entityAt = map->functions().at("entityAt");
    EXPECT_TRUE(entityAt.bVariadic);
    EXPECT_TRUE(entityAt.isConst());
    ASSERT_EQ(entityAt.args.size(), 3u);
    EXPECT_EQ(entityAt.args[2].type, ya::type_index_v<ya::script::ScriptRef>);

    Class* entity = ClassRegistry::instance().getClass(ya::type_index_v<ya::Entity>);
    ASSERT_NE(entity, nullptr);
    const Function& call = entity->functions().at("call");
    EXPECT_TRUE(call.bVariadic);
    ASSERT_EQ(call.args.size(), 1u);
    EXPECT_EQ(call.args[0].type, ya::type_index_v<std::string>);

    Class* timer = ClassRegistry::instance().getClass("ya::WidgetScriptTimer");
    ASSERT_NE(timer, nullptr);
    EXPECT_TRUE(timer->functions().contains("cancel"));
    EXPECT_FALSE(timer->functions().at("cancel").bVariadic);

    Class* clicked = ClassRegistry::instance().getClass("ya::WidgetScriptButtonClicked");
    ASSERT_NE(clicked, nullptr);
    const Function& add = clicked->functions().at("add");
    EXPECT_TRUE(add.bVariadic);
    ASSERT_EQ(add.args.size(), 2u);
    EXPECT_EQ(add.args[0].type, ya::type_index_v<ya::script::ScriptSelfHandle>);
    EXPECT_EQ(add.args[1].type, ya::type_index_v<ya::script::ScriptFnHandle>);
    EXPECT_TRUE(clicked->functions().at("remove").bVariadic);
    EXPECT_EQ(clicked->functions().at("removeAll").args[0].type, ya::type_index_v<ya::script::ScriptSelfHandle>);
}
