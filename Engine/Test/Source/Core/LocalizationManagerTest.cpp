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
    manager.onLanguageChanged = [&](std::string_view language) {
        ++fired;
        seen = std::string(language);
    };

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

TEST(LocalizationManagerTest, InvalidTablesAreRejected)
{
    auto& manager = LocalizationManager::get();
    manager.clear();
    EXPECT_FALSE(manager.loadLanguageFromJson("zh", "{not json"));
    EXPECT_FALSE(manager.loadLanguageFromJson("zh", R"(["array"])"));
    EXPECT_TRUE(manager.loadedLanguages().empty());
}

} // namespace ya
