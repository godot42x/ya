#include "ECS/Systems/Components/LuaScriptComponent.h"

#include <any>
#include <gtest/gtest.h>
#include <sol/sol.hpp>
#include <string>
#include <unordered_map>

namespace ya
{

TEST(LuaScriptComponentLifetimeTest, CapturedPropertiesSurviveLuaStateDeath)
{
    LuaScriptComponent::ScriptInstance script;
    script.scriptPath = "Content/Scripts/Spin.lua";
    script.propertyOverrides["radius"] = 3.5f;

    {
        sol::state lua;
        lua.open_libraries(sol::lib::base, sol::lib::table);
        sol::table table = lua.create_table();
        sol::table props = lua.create_table();
        sol::table radius = lua.create_table();
        radius["value"] = 5.0f;
        radius["type"]  = "float";
        props["radius"] = radius;
        table["_PROPERTIES"] = props;

        script.applyPropertyOverridesTo(table, lua);
        script.capturePropertiesFrom(table);
        script.self = table;
        script.releaseLuaHandles();

        EXPECT_FALSE(script.self.valid());
        ASSERT_EQ(script.properties.size(), 1u);
        EXPECT_EQ(script.properties[0].name, "radius");
        ASSERT_TRUE(script.properties[0].value.has_value());
        EXPECT_FLOAT_EQ(std::any_cast<float>(script.properties[0].value), 3.5f);
        EXPECT_EQ(std::any_cast<float>(script.propertyOverrides["radius"]), 3.5f);
    }

    ASSERT_EQ(script.properties.size(), 1u);
    EXPECT_FLOAT_EQ(std::any_cast<float>(script.properties[0].value), 3.5f);
}

TEST(LuaScriptComponentLifetimeTest, CapturePropertiesFromMultipleEntries)
{
    sol::state lua;
    lua.open_libraries(sol::lib::base, sol::lib::table);

    sol::table table = lua.create_table();
    table["moveSpeed"] = 8.0f;
    table["lookSpeed"] = 45.0f;
    table["enabled"]   = true;

    sol::table props = lua.create_table();
    auto addProp = [&](const char* name, auto value, const char* type, float min, float max) {
        sol::table def = lua.create_table();
        def["value"]   = value;
        def["type"]    = type;
        def["min"]     = min;
        def["max"]     = max;
        props[name]    = def;
    };
    addProp("moveSpeed", 8.0f, "float", 0.1f, 50.0f);
    addProp("lookSpeed", 45.0f, "float", 1.0f, 180.0f);
    addProp("enabled", true, "bool", 0.0f, 1.0f);
    addProp("name", std::string{"camera"}, "string", 0.0f, 0.0f);
    table["_PROPERTIES"] = props;

    LuaScriptComponent::ScriptInstance script;
    script.capturePropertiesFrom(table);

    ASSERT_EQ(script.properties.size(), 4u);
    std::unordered_map<std::string, LuaScriptComponent::ScriptProperty> byName;
    for (auto& prop : script.properties) {
        byName.emplace(prop.name, prop);
    }
    ASSERT_TRUE(byName.contains("moveSpeed"));
    ASSERT_TRUE(byName.contains("lookSpeed"));
    ASSERT_TRUE(byName.contains("enabled"));
    ASSERT_TRUE(byName.contains("name"));
    EXPECT_FLOAT_EQ(std::any_cast<float>(byName["moveSpeed"].value), 8.0f);
    EXPECT_FLOAT_EQ(std::any_cast<float>(byName["lookSpeed"].value), 45.0f);
    EXPECT_TRUE(std::any_cast<bool>(byName["enabled"].value));
    EXPECT_EQ(std::any_cast<std::string>(byName["name"].value), "camera");
    EXPECT_FLOAT_EQ(byName["moveSpeed"].min, 0.1f);
    EXPECT_FLOAT_EQ(byName["lookSpeed"].max, 180.0f);
    EXPECT_EQ(table.get<std::string>("name"), "camera");
}

TEST(LuaScriptComponentLifetimeTest, CloneCustomDropsLuaHandlesAndKeepsOverrides)
{
    sol::state lua;
    lua.open_libraries(sol::lib::base);

    LuaScriptComponent src;
    auto* instance = src.addScript("Content/Scripts/Spin.lua");
    ASSERT_NE(instance, nullptr);
    instance->enabled = false;
    instance->propertyOverrides["speed"] = 12;
    instance->self = lua.create_table();
    instance->bAuthoringPreviewLoaded = true;

    LuaScriptComponent dst;
    dst.cloneCustom(src);

    ASSERT_EQ(dst.scripts.size(), 1u);
    EXPECT_EQ(dst.scripts[0].scriptPath, instance->scriptPath);
    EXPECT_FALSE(dst.scripts[0].enabled);
    EXPECT_FALSE(dst.scripts[0].self.valid());
    EXPECT_FALSE(dst.scripts[0].bLoaded);
    EXPECT_FALSE(dst.scripts[0].bAuthoringPreviewLoaded);
    EXPECT_TRUE(dst.scripts[0].properties.empty());
    ASSERT_EQ(dst.scripts[0].propertyOverrides.size(), 1u);
    EXPECT_EQ(std::any_cast<int>(dst.scripts[0].propertyOverrides["speed"]), 12);

    src.releaseLuaHandles();
}

} // namespace ya
