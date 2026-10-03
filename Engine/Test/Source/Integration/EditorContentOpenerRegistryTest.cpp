#include "GameEditor/UI/Shell/EditorContentOpenerRegistry.h"

#include <gtest/gtest.h>

namespace ya
{
namespace
{

void openA(EditorLayer&, std::string) {}
void openB(EditorLayer&, std::string) {}
void openShort(EditorLayer&, std::string) {}
void openLong(EditorLayer&, std::string) {}

using OpenerFn = void (*)(EditorLayer&, std::string);

[[nodiscard]] const OpenerFn* storedFn(const EditorContentOpener* opener)
{
    return opener != nullptr ? opener->target<OpenerFn>() : nullptr;
}

[[nodiscard]] bool hasExtension(const EditorContentOpenerRegistry& registry, std::string_view extension)
{
    for (const FEditorContentOpener& entry : registry.all()) {
        if (entry.extension == extension) {
            return true;
        }
    }
    return false;
}

} // namespace

TEST(EditorContentOpenerRegistryTest, LongestSuffixWins)
{
    EditorContentOpenerRegistry registry;
    registry.registerOpener(".json", &openShort);
    registry.registerOpener(".scene.json", &openLong);
    registry.registerOpener(".mat", &openShort);
    registry.registerOpener(".material", &openLong);

    const OpenerFn* scene = storedFn(registry.find("Content/Town.scene.json"));
    ASSERT_NE(scene, nullptr);
    EXPECT_EQ(*scene, &openLong);

    const OpenerFn* notes = storedFn(registry.find("Content/notes.json"));
    ASSERT_NE(notes, nullptr);
    EXPECT_EQ(*notes, &openShort);

    const OpenerFn* material = storedFn(registry.find("Lit.material"));
    ASSERT_NE(material, nullptr);
    EXPECT_EQ(*material, &openLong);

    const OpenerFn* mat = storedFn(registry.find("Lit.mat"));
    ASSERT_NE(mat, nullptr);
    EXPECT_EQ(*mat, &openShort);

    const OpenerFn* embedded = storedFn(registry.find("notscene.json"));
    ASSERT_NE(embedded, nullptr);
    EXPECT_EQ(*embedded, &openShort);
    EXPECT_EQ(registry.find("Town.scene.json.bak"), nullptr);
}

TEST(EditorContentOpenerRegistryTest, MatchIsCaseInsensitive)
{
    EditorContentOpenerRegistry registry;
    registry.registerOpener(".SCENE.JSON", &openLong);

    const OpenerFn* lower = storedFn(registry.find("Town.scene.json"));
    ASSERT_NE(lower, nullptr);
    EXPECT_EQ(*lower, &openLong);

    const OpenerFn* upper = storedFn(registry.find("Town.SCENE.JSON"));
    ASSERT_NE(upper, nullptr);
    EXPECT_EQ(*upper, &openLong);

    const OpenerFn* mixed = storedFn(registry.find("Town.Scene.Json"));
    ASSERT_NE(mixed, nullptr);
    EXPECT_EQ(*mixed, &openLong);
}

TEST(EditorContentOpenerRegistryTest, UnregisteredPathReturnsNull)
{
    EditorContentOpenerRegistry registry;
    EXPECT_TRUE(registry.all().empty());
    EXPECT_EQ(registry.find("Town.scene.json"), nullptr);
    EXPECT_EQ(registry.find(""), nullptr);
    EXPECT_EQ(registry.find("readme.txt"), nullptr);

    registry.registerOpener(".lua", &openA);
    EXPECT_EQ(registry.find("readme.txt"), nullptr);
    EXPECT_EQ(registry.find("Hero.yaanim.json"), nullptr);
}

TEST(EditorContentOpenerRegistryTest, LaterRegistrationOverridesAndAppends)
{
    EditorContentOpenerRegistry registry;
    registry.registerOpener(".lua", &openA);
    registry.registerOpener("lua", &openB);
    registry.registerOpener(".LUA", EditorContentOpener{});
    EXPECT_EQ(registry.all().size(), 1u);
    const OpenerFn* lua = storedFn(registry.find("Hero.LUA"));
    ASSERT_NE(lua, nullptr);
    EXPECT_EQ(*lua, &openB);

    registry.registerOpener(".txt", &openA);
    EXPECT_EQ(registry.all().size(), 2u);
    const OpenerFn* text = storedFn(registry.find("notes.TXT"));
    ASSERT_NE(text, nullptr);
    EXPECT_EQ(*text, &openA);
    EXPECT_EQ(*storedFn(registry.find("Hero.lua")), &openB);
}

TEST(EditorContentOpenerRegistryTest, BuiltinOpenersIncludeTheFourContentSuffixes)
{
    EditorContentOpenerRegistry registry;
    registerBuiltinContentOpeners(registry);

    EXPECT_TRUE(hasExtension(registry, ".scene.json"));
    EXPECT_TRUE(hasExtension(registry, ".lua"));
    EXPECT_TRUE(hasExtension(registry, ".yaui.json"));
    EXPECT_TRUE(hasExtension(registry, ".mat"));
    EXPECT_TRUE(hasExtension(registry, ".material"));
    EXPECT_EQ(registry.all().size(), 5u);

    EXPECT_NE(registry.find("Town.scene.json"), nullptr);
    EXPECT_NE(registry.find("Hero.LUA"), nullptr);
    EXPECT_NE(registry.find("Panel.yaui.json"), nullptr);
    EXPECT_NE(registry.find("Lit.MAT"), nullptr);
    EXPECT_NE(registry.find("Lit.MATERIAL"), nullptr);
    EXPECT_NE(registry.find("Lit.material"), registry.find("Lit.mat"));
    EXPECT_EQ(registry.find("Hero.yaanim.json"), nullptr);
    EXPECT_EQ(registry.find("notes.txt"), nullptr);

    registerBuiltinContentOpeners(registry);
    EXPECT_EQ(registry.all().size(), 5u);
}

TEST(EditorContentOpenerRegistryTest, ProcessRegistryIncludesBuiltinOpeners)
{
    const EditorContentOpenerRegistry& registry = EditorContentOpenerRegistry::get();
    EXPECT_NE(registry.find("Town.scene.json"), nullptr);
    EXPECT_NE(registry.find("Hero.lua"), nullptr);
    EXPECT_NE(registry.find("Panel.yaui.json"), nullptr);
    EXPECT_NE(registry.find("Lit.mat"), nullptr);
    EXPECT_NE(registry.find("Lit.material"), nullptr);
    EXPECT_EQ(&EditorContentOpenerRegistry::get(), &registry);
}

} // namespace ya
