#include "ECS/Systems/Components/LuaScriptComponent.h"

#include <gtest/gtest.h>
#include <nlohmann/json.hpp>
#include <sol/sol.hpp>
#include <string>
#include <unordered_map>
#include <variant>

namespace ya
{

TEST(LuaScriptComponentLifetimeTest, CapturedPropertiesSurviveLuaStateDeath)
{
    LuaScriptInstance script;
    script.scriptPath = "Content/Scripts/Spin.lua";
    script.propertyOverrides["radius"] = 3.5;

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
        ASSERT_TRUE(std::holds_alternative<double>(script.properties[0].value));
        EXPECT_DOUBLE_EQ(std::get<double>(script.properties[0].value), 3.5);
        EXPECT_DOUBLE_EQ(std::get<double>(script.propertyOverrides["radius"]), 3.5);
    }

    ASSERT_EQ(script.properties.size(), 1u);
    EXPECT_DOUBLE_EQ(std::get<double>(script.properties[0].value), 3.5);
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

    LuaScriptInstance script;
    script.capturePropertiesFrom(table);

    ASSERT_EQ(script.properties.size(), 4u);
    std::unordered_map<std::string, LuaScriptProperty> byName;
    for (auto& prop : script.properties) {
        byName.emplace(prop.name, prop);
    }
    ASSERT_TRUE(byName.contains("moveSpeed"));
    ASSERT_TRUE(byName.contains("lookSpeed"));
    ASSERT_TRUE(byName.contains("enabled"));
    ASSERT_TRUE(byName.contains("name"));
    EXPECT_DOUBLE_EQ(std::get<double>(byName["moveSpeed"].value), 8.0);
    EXPECT_DOUBLE_EQ(std::get<double>(byName["lookSpeed"].value), 45.0);
    EXPECT_TRUE(std::get<bool>(byName["enabled"].value));
    EXPECT_EQ(std::get<std::string>(byName["name"].value), "camera");
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
    instance->propertyOverrides["speed"] = int64_t{12};
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
    EXPECT_EQ(std::get<int64_t>(dst.scripts[0].propertyOverrides["speed"]), 12);

    src.releaseLuaHandles();
}

TEST(LuaScriptComponentLifetimeTest, PropertyOverrideJsonMatchesLegacyEncoding)
{
    nlohmann::json legacy = nlohmann::json::object();
    legacy["speed"]      = 12;
    legacy["radius"]     = 3.5f;
    legacy["bias"]       = 0.1f;
    legacy["whole"]      = 8.0f;
    legacy["scale"]      = 0.1;
    legacy["enabled"]    = true;
    legacy["name"]       = "camera";
    legacy["spawnName"]  = "SpawnFromHouse";
    legacy["uv"]         = nlohmann::json::array({0.25f, 0.5f});
    legacy["offset"]     = nlohmann::json::array({1.0f, 2.0f, 3.5f});
    legacy["tint"]       = nlohmann::json::array({1.0f, 0.0f, 0.0f, 1.0f});

    LuaScriptComponent authored;
    auto* script = authored.addScript("Content/Scripts/Spin.lua");
    ASSERT_NE(script, nullptr);
    script->executionOrderOverride = 3;
    script->propertyOverrides["speed"]     = int64_t{12};
    script->propertyOverrides["radius"]    = static_cast<double>(3.5f);
    script->propertyOverrides["bias"]      = static_cast<double>(0.1f);
    script->propertyOverrides["whole"]     = static_cast<double>(8.0f);
    script->propertyOverrides["scale"]     = 0.1;
    script->propertyOverrides["enabled"]   = true;
    script->propertyOverrides["name"]      = std::string{"camera"};
    script->propertyOverrides["spawnName"] = std::string{"SpawnFromHouse"};
    script->propertyOverrides["uv"]        = glm::vec2{0.25f, 0.5f};
    script->propertyOverrides["offset"]    = glm::vec3{1.0f, 2.0f, 3.5f};
    script->propertyOverrides["tint"]      = glm::vec4{1.0f, 0.0f, 0.0f, 1.0f};

    nlohmann::json authoredJson;
    authored.serializeCustom(authoredJson);
    const auto& authoredOverrides = authoredJson["scripts"][0]["propertyOverrides"];
    EXPECT_EQ(authoredJson["scripts"][0]["executionOrder"], 3);
    EXPECT_EQ(authoredOverrides, legacy);
    for (auto it = legacy.begin(); it != legacy.end(); ++it) {
        EXPECT_EQ(authoredOverrides.at(it.key()).dump(), it.value().dump()) << it.key();
    }
    EXPECT_TRUE(authoredOverrides.at("speed").is_number_integer());
    EXPECT_TRUE(authoredOverrides.at("whole").is_number_float());
    EXPECT_NE(authoredOverrides.at("whole").dump(), "8");

    nlohmann::json legacyDoc;
    legacyDoc["scripts"] = nlohmann::json::array({nlohmann::json{
        {"scriptPath", "Content/Scripts/Spin.lua"},
        {"enabled", true},
        {"executionOrder", 3},
        {"propertyOverrides", legacy},
    }});

    LuaScriptComponent loaded;
    loaded.deserializeCustom(legacyDoc);
    nlohmann::json saved;
    loaded.serializeCustom(saved);
    const auto& loadedOverrides = saved["scripts"][0]["propertyOverrides"];
    EXPECT_EQ(loadedOverrides, legacy);
    for (auto it = legacy.begin(); it != legacy.end(); ++it) {
        EXPECT_EQ(loadedOverrides.at(it.key()).dump(), it.value().dump()) << it.key();
    }

    LuaScriptComponent again;
    again.deserializeCustom(saved);
    nlohmann::json resaved;
    again.serializeCustom(resaved);
    const auto& resavedOverrides = resaved["scripts"][0]["propertyOverrides"];
    EXPECT_EQ(resavedOverrides, loadedOverrides);
    for (auto it = loadedOverrides.begin(); it != loadedOverrides.end(); ++it) {
        EXPECT_EQ(resavedOverrides.at(it.key()).dump(), it.value().dump()) << it.key();
    }
}

} // namespace ya
