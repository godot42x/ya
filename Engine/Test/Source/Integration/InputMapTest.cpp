#include "App/Module/ProjectDescriptor.h"
#include "Core/Input/InputManager.h"
#include "Core/Log.h"

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>

namespace ya
{
namespace
{

class InputMapTest : public ::testing::Test
{
  protected:
    std::filesystem::path _root;

    void SetUp() override
    {
        _root = std::filesystem::temp_directory_path() /
                ("ya-input-map-test-" + std::to_string(::testing::UnitTest::GetInstance()->random_seed()) + "-" +
                 std::to_string(::testing::UnitTest::GetInstance()->current_test_info()->line()));
        std::filesystem::remove_all(_root);
        std::filesystem::create_directories(_root / "Content");
        writeText(_root / "Game.yamodule",
                  R"({
  "schemaVersion": 1,
  "name": "Game",
  "kind": "project",
  "binary": "Game",
  "dependencies": []
})");
    }

    void TearDown() override
    {
        std::error_code error;
        std::filesystem::remove_all(_root, error);
    }

    std::filesystem::path writeText(const std::filesystem::path& path, std::string_view content) const
    {
        std::filesystem::create_directories(path.parent_path());
        std::ofstream stream(path);
        EXPECT_TRUE(stream.is_open());
        stream << content;
        return path;
    }

    std::filesystem::path writeProject(std::string_view inputMapField) const
    {
        const std::string text = std::string(R"({
  "schemaVersion": 1,
  "name": "Game",
  "mainModule": "Game",
  "modules": ["Game.yamodule"],
  "plugins": [],
  "contentDir": "Content",
  "inputMap": )") + std::string(inputMapField) + "\n}\n";
        return writeText(_root / "Game.yaproject", text);
    }

    static void press(InputManager& input, EKey::T key)
    {
        KeyPressedEvent event{};
        event._keyCode = key;
        input.processEvent(event);
    }
};

/// Counts `Input: no action` warnings. Restores the core formatter on the way out.
struct MissingActionWarnCount
{
    int count = 0;
    logcc::LoggerBase::formatter_t previous;

    MissingActionWarnCount()
    {
        auto& logger = Logger::core();
        previous = logger.formatter;
        logger.setFormatter([this](const logcc::Config& config, std::string& output, logcc::LogLevel::T level,
                                   std::string_view message, const std::source_location& location) {
            if (level == logcc::LogLevel::Warn && message.find("no action") != std::string_view::npos) {
                ++count;
            }
            return previous ? previous(config, output, level, message, location) : true;
        });
    }

    ~MissingActionWarnCount() { Logger::core().setFormatter(std::move(previous)); }
};

TEST_F(InputMapTest, ReadsJsonAndAnyListedKey)
{
    writeText(_root / "InputMap.json",
              R"({
  "move_up": ["Up", "W"],
  "move_down": ["Down", "S"],
  "confirm": ["Space", "Enter", "E"]
})");
    const auto descriptor = FProjectDescriptor::load(writeProject("\"InputMap.json\""));
    ASSERT_TRUE(descriptor.bInputMapLoaded);
    ASSERT_TRUE(descriptor.inputMap.has_value());
    EXPECT_TRUE(std::filesystem::is_regular_file(*descriptor.inputMap));
    ASSERT_EQ(descriptor.inputActions.at("move_up").size(), 2u);
    EXPECT_EQ(descriptor.inputActions.at("move_up")[0], "Up");
    EXPECT_EQ(descriptor.inputActions.at("move_up")[1], "W");

    InputManager input;
    input.configureActionBindings(descriptor.inputActions, !descriptor.inputMap.has_value());
    input.init();

    press(input, EKey::K_W);
    EXPECT_TRUE(input.isActionPressed("move_up"));
    EXPECT_FALSE(input.isActionPressed("move_down"));

    input.preUpdate();
    KeyReleasedEvent released{};
    released._keyCode = EKey::K_W;
    input.processEvent(released);
    press(input, EKey::Up);
    EXPECT_TRUE(input.isActionPressed("move_up"));

    input.preUpdate();
    press(input, EKey::K_E);
    EXPECT_TRUE(input.isActionPressed("confirm"));
    EXPECT_FALSE(input.wasActionPressed("move_up"));
}

TEST_F(InputMapTest, MissingActionReturnsFalse)
{
    writeText(_root / "InputMap.json", R"({ "confirm": ["Space"] })");
    const auto descriptor = FProjectDescriptor::load(writeProject("\"InputMap.json\""));
    ASSERT_TRUE(descriptor.bInputMapLoaded);

    InputManager input;
    input.configureActionBindings(descriptor.inputActions, false);
    EXPECT_FALSE(input.isActionPressed("jump"));
    EXPECT_FALSE(input.wasActionPressed("jump"));
    EXPECT_FALSE(input.wasActionReleased("jump"));
}

TEST_F(InputMapTest, MissingActionWarnsOnce)
{
    writeText(_root / "InputMap.json", R"({ "confirm": ["Space"] })");
    const auto descriptor = FProjectDescriptor::load(writeProject("\"InputMap.json\""));

    MissingActionWarnCount warnings;

    InputManager input;
    input.configureActionBindings(descriptor.inputActions, false);
    EXPECT_FALSE(input.isActionPressed("jump"));
    EXPECT_FALSE(input.isActionPressed("jump"));
    EXPECT_FALSE(input.wasActionPressed("jump"));
    EXPECT_EQ(warnings.count, 1);

    input.configureActionBindings(descriptor.inputActions, false);
    EXPECT_FALSE(input.isActionPressed("jump"));
    EXPECT_FALSE(input.isActionPressed("jump"));
    EXPECT_EQ(warnings.count, 2);
}

TEST_F(InputMapTest, PressedEdgeIsOneFrame)
{
    writeText(_root / "InputMap.json", R"({ "confirm": ["Space", "E"] })");
    const auto descriptor = FProjectDescriptor::load(writeProject("\"InputMap.json\""));
    InputManager input;
    input.configureActionBindings(descriptor.inputActions, false);

    press(input, EKey::K_E);
    EXPECT_TRUE(input.wasActionPressed("confirm"));
    EXPECT_TRUE(input.isActionPressed("confirm"));

    input.preUpdate();
    EXPECT_FALSE(input.wasActionPressed("confirm"));
    EXPECT_TRUE(input.isActionPressed("confirm"));
}

TEST_F(InputMapTest, MissingFileReturnsFalse)
{
    const auto descriptor = FProjectDescriptor::load(writeProject("\"NoSuch.json\""));
    EXPECT_FALSE(descriptor.bInputMapLoaded);
    EXPECT_TRUE(descriptor.inputMap.has_value());
    EXPECT_TRUE(descriptor.inputActions.empty());

    InputManager input;
    input.configureActionBindings(descriptor.inputActions, !descriptor.inputMap.has_value());
    input.init();
    press(input, EKey::K_W);
    EXPECT_FALSE(input.isActionPressed("move_up"));
    EXPECT_FALSE(input.isActionPressed("move_forward"));
}

TEST_F(InputMapTest, InvalidFileReturnsFalse)
{
    writeText(_root / "InputMap.json", "{");
    const auto descriptor = FProjectDescriptor::load(writeProject("\"InputMap.json\""));
    EXPECT_FALSE(descriptor.bInputMapLoaded);
    EXPECT_TRUE(descriptor.inputActions.empty());

    writeText(_root / "InputMap.json", R"({ "move_up": 1 })");
    const auto shaped = FProjectDescriptor::load(writeProject("\"InputMap.json\""));
    EXPECT_FALSE(shaped.bInputMapLoaded);
    EXPECT_TRUE(shaped.inputActions.empty());
}

} // namespace
} // namespace ya
