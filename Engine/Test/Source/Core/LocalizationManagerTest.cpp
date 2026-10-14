#include "Core/Localization/LocalizationManager.h"

#include <gtest/gtest.h>

namespace ya
{

// A missing key translates to the key itself, so content works before its
// translation is written.
TEST(LocalizationManagerTest, MissingKeyPassesThrough)
{
    auto& manager = LocalizationManager::get();
    manager.clear();
    EXPECT_EQ(manager.tr("Attack"), "Attack");
}

TEST(LocalizationManagerTest, LoadTableAndTranslate)
{
    auto& manager = LocalizationManager::get();
    manager.clear();
    ASSERT_TRUE(manager.loadLanguageFromJson("zh", R"({"Attack": "攻击", "Wait": "等待"})"));
    manager.setLanguage("zh");
    EXPECT_EQ(manager.language(), "zh");
    EXPECT_EQ(manager.tr("Attack"), "攻击");
    EXPECT_EQ(manager.tr("Wait"), "等待");
    // A key the table does not have falls back to the key itself.
    EXPECT_EQ(manager.tr("Skill"), "Skill");
    manager.setLanguage("");
    EXPECT_EQ(manager.tr("Attack"), "Attack");
}

TEST(LocalizationManagerTest, SetLanguageFiresChangeOnce)
{
    auto& manager = LocalizationManager::get();
    manager.clear();
    ASSERT_TRUE(manager.loadLanguageFromJson("zh", R"({"Attack": "攻击"})"));

    int         fired  = 0;
    std::string seen;
    const auto handle = manager.addLanguageChangedListener([&](std::string_view language) {
        ++fired;
        seen = std::string(language);
    });

    manager.setLanguage("zh");
    EXPECT_EQ(fired, 1);
    EXPECT_EQ(seen, "zh");
    // Same language again: no event.
    manager.setLanguage("zh");
    EXPECT_EQ(fired, 1);
    // A language with no table still switches (keys pass through) and fires.
    manager.setLanguage("fr");
    EXPECT_EQ(fired, 2);
    EXPECT_EQ(seen, "fr");
    EXPECT_EQ(manager.tr("Attack"), "Attack");
    manager.removeLanguageChangedListener(handle);
}

// Every listener gets the event; removing one leaves the others subscribed.
TEST(LocalizationManagerTest, MultipleListeners)
{
    auto& manager = LocalizationManager::get();
    manager.clear();
    ASSERT_TRUE(manager.loadLanguageFromJson("zh", R"({"Attack": "攻击"})"));
    ASSERT_TRUE(manager.loadLanguageFromJson("en", R"({"Attack": "Attack"})"));

    int count1 = 0;
    int count2 = 0;
    const auto h1 = manager.addLanguageChangedListener([&](std::string_view) { ++count1; });
    const auto h2 = manager.addLanguageChangedListener([&](std::string_view) { ++count2; });

    manager.setLanguage("zh");
    EXPECT_EQ(count1, 1);
    EXPECT_EQ(count2, 1);

    manager.removeLanguageChangedListener(h1);
    manager.setLanguage("en");
    EXPECT_EQ(count1, 1); // no longer notified
    EXPECT_EQ(count2, 2);
    manager.removeLanguageChangedListener(h2);
}

// `tr(key, fallback)` prefers the table, then the fallback source string, then
// the key. A short key already resolves to itself without a fallback.
TEST(LocalizationManagerTest, FallbackSourceServesMissingKey)
{
    auto& manager = LocalizationManager::get();
    manager.clear();
    ASSERT_TRUE(manager.loadLanguageFromJson("zh", R"({"editor.tab.level": "关卡"})"));
    manager.setLanguage("zh");
    EXPECT_EQ(manager.tr("editor.tab.level", "Level"), "关卡");
    // Not in the table: the source string is what the user reads.
    EXPECT_EQ(manager.tr("editor.inspector.empty", "Select an entity"), "Select an entity");
    // No fallback and no table: the key itself, still displayable.
    EXPECT_EQ(manager.tr("editor.inspector.empty"), "editor.inspector.empty");
}

TEST(LocalizationManagerTest, FallbackChainServesMissingActiveKeys)
{
    auto& manager = LocalizationManager::get();
    manager.clear();
    ASSERT_TRUE(manager.loadLanguageFromJson("zh", R"({"Attack": "攻击"})"));
    ASSERT_TRUE(manager.loadLanguageFromJson("fr", R"({"Attack": "Attaque", "Wait": "Attendre"})"));
    manager.setFallbackLanguages({"fr"});
    manager.setLanguage("zh");
    EXPECT_EQ(manager.tr("Attack"), "攻击");
    EXPECT_EQ(manager.tr("Wait"), "Attendre");
    EXPECT_EQ(manager.tr("Skill"), "Skill");
}

// `{N}` placeholders are filled positionally. A placeholder with no matching
// argument stays written so a mistyped index is visible.
TEST(LocalizationManagerTest, Format)
{
    auto& manager = LocalizationManager::get();
    manager.clear();
    ASSERT_TRUE(manager.loadLanguageFromJson("test", R"({"Set {0}": "设置{0}"})"));
    manager.setLanguage("test");
    EXPECT_EQ(manager.format(manager.tr("Set {0}"), {"Transform"}), "设置Transform");
    // No placeholders: the text passes through unchanged.
    EXPECT_EQ(manager.format("plain", {"unused"}), "plain");
    // Multi-arg and out-of-range index.
    EXPECT_EQ(LocalizationManager::format("{0}-{1}", {"a", "b"}), "a-b");
    EXPECT_EQ(LocalizationManager::format("{2}", {"a"}), "{2}");
}

// A key that misses while a language is active is recorded for diagnostics.
TEST(LocalizationManagerTest, MissingKeysAreRecorded)
{
    auto& manager = LocalizationManager::get();
    manager.clear();
    ASSERT_TRUE(manager.loadLanguageFromJson("zh", R"({"Attack": "攻击"})"));
    manager.setLanguage("zh");
    (void)manager.tr("Attack");
    (void)manager.tr("editor.unknown.key");
    const auto& missing = manager.missingKeys();
    EXPECT_EQ(missing.count("Attack"), 0u);
    EXPECT_EQ(missing.count("editor.unknown.key"), 1u);
    manager.clear();
    EXPECT_TRUE(manager.missingKeys().empty());
}

TEST(LocalizationManagerTest, InvalidTablesAreRejected)
{
    auto& manager = LocalizationManager::get();
    manager.clear();
    EXPECT_FALSE(manager.loadLanguageFromJson("zh", "{not json"));
    EXPECT_FALSE(manager.loadLanguageFromJson("zh", R"(["array"])"));
    EXPECT_TRUE(manager.loadedLanguages().empty());
}

} // namespace ya
