// LocalizedText: a key + args handle whose reactive value re-resolves when the
// active language changes. Guards the P0.3 contract the editor's dock tab
// titles bind to (a language switch must update bound text with no rebuild).

#include "Core/Localization/LocalizationManager.h"
#include "GUI/Binding/LocalizedText.h"

#include <gtest/gtest.h>

namespace ya
{

TEST(LocalizedTextTest, ResolvesThroughManagerAndReactiveFollowsLanguage)
{
    auto& manager = LocalizationManager::get();
    manager.clear();
    ASSERT_TRUE(manager.loadLanguageFromJson("zh", R"({"editor.tab.level": "关卡"})"));

    LocalizedText text("editor.tab.level", {});
    // No language yet: the key resolves to itself.
    EXPECT_EQ(text.resolve(), "editor.tab.level");

    auto reactive = text.toReactive();
    ASSERT_TRUE(reactive != nullptr);
    EXPECT_EQ(reactive->value(), "editor.tab.level");

    manager.setLanguage("zh");
    EXPECT_EQ(text.resolve(), "关卡");
    // The bound reactive followed the language change without a rebuild.
    EXPECT_EQ(reactive->value(), "关卡");

    manager.setLanguage("");
    EXPECT_EQ(reactive->value(), "editor.tab.level");
    manager.clear();
}

TEST(LocalizedTextTest, FormatArgumentsFillPlaceholders)
{
    auto& manager = LocalizationManager::get();
    manager.clear();
    ASSERT_TRUE(manager.loadLanguageFromJson("zh", R"({"editor.set": "设置 {0}"})"));
    manager.setLanguage("zh");

    LocalizedText text("editor.set", {"Transform"});
    EXPECT_EQ(text.resolve(), "设置 Transform");
    manager.clear();
}

TEST(LocalizedTextTest, CopiesShareOneReactiveValue)
{
    auto& manager = LocalizationManager::get();
    manager.clear();
    ASSERT_TRUE(manager.loadLanguageFromJson("zh", R"({"editor.tab.level": "关卡"})"));

    LocalizedText original("editor.tab.level");
    auto         reactive = original.toReactive();
    LocalizedText copy     = original;
    EXPECT_EQ(copy.toReactive(), reactive);

    manager.setLanguage("zh");
    EXPECT_EQ(copy.resolve(), "关卡");
    EXPECT_EQ(reactive->value(), "关卡");
    manager.clear();
}

TEST(LocalizedTextTest, EmptyHandleIsInert)
{
    LocalizedText empty;
    EXPECT_TRUE(empty.empty());
    EXPECT_EQ(empty.toReactive(), nullptr);
    EXPECT_EQ(empty.resolve(), "");
}

} // namespace ya
