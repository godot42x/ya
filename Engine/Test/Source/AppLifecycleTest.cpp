#include "Runtime/App/Lifecycle/AppLifecycle.h"

#include "Runtime/App/App.h"
#include "Runtime/App/IAppExtension.h"

#include "Scene/SceneManager.h"

#include <gtest/gtest.h>

#include <string>
#include <utility>
#include <vector>

namespace ya
{

class AppExtensionTestAccess
{
  public:
    static void configure(App& app) { app.configureExtensions(); }
    static void attach(App& app) { app.attachExtensions(); }
    static void detach(App& app) { app.detachExtensions(); }
    static void dispatchNativeEvent(App& app, const SDL_Event& event) { app.dispatchNativeEvent(event); }
    static bool dispatchEvent(App& app, const Event& event) { return app.dispatchExtensionEvent(event); }
    static void tick(App& app, float dt) { app.tickExtensions(dt); }
    static void prepareRender(App& app, float dt) { app.prepareExtensionsForRender(dt); }
};

namespace
{

struct RecordingExtension final : IAppExtension
{
    std::vector<std::string>& calls;
    std::string               name;
    bool                      consumesEvents = false;

    RecordingExtension(std::vector<std::string>& calls, std::string name, bool consumesEvents = false)
        : calls(calls), name(std::move(name)), consumesEvents(consumesEvents)
    {
    }

    void onConfigure(App&, AppDesc&) override { calls.push_back(name + ".configure"); }
    void onAttach(App&) override { calls.push_back(name + ".attach"); }
    void onDetach(App&) override { calls.push_back(name + ".detach"); }
    bool onBeforeAppStateChange(App&, AppState, AppState) override
    {
        calls.push_back(name + ".before-state");
        return true;
    }
    void onAfterAppStateChange(App&, AppState, AppState) override { calls.push_back(name + ".after-state"); }
    void onNativeEvent(App&, const SDL_Event&) override { calls.push_back(name + ".native"); }
    bool onEvent(App&, const Event&) override
    {
        calls.push_back(name + ".event");
        return consumesEvents;
    }
    void onLogic(App&, float) override { calls.push_back(name + ".logic"); }
    void onBeforeRender(App&, float) override { calls.push_back(name + ".before-render"); }
};

class AppLifecycleTest : public ::testing::Test
{
  protected:
    App app;
    std::unique_ptr<SceneManager> sceneManager;

    void SetUp() override
    {
        sceneManager = std::make_unique<SceneManager>();
        app._sceneManager = sceneManager.get();
        app._appState     = AppState::Stopped;
    }

    void TearDown() override
    {
        app._sceneManager = nullptr;
        sceneManager.reset();
    }
};

TEST_F(AppLifecycleTest, LoadSceneIgnoresEmptyPathWithoutCreatingFallbackScene)
{
    EXPECT_FALSE(sceneManager->hasScene());

    const bool bLoaded = AppLifecycle::loadScene(app, "");

    EXPECT_FALSE(bLoaded);
    EXPECT_FALSE(sceneManager->hasScene());
    EXPECT_EQ(sceneManager->getActiveScene(), nullptr);
}

TEST_F(AppLifecycleTest, ActiveSceneSwitchKeepsCallerOwnedScenesAlive)
{
    auto authoringScene = makeShared<Scene>("Authoring");
    ASSERT_TRUE(sceneManager->activateScene(authoringScene));

    auto playScene = sceneManager->cloneScene(authoringScene.get());
    ASSERT_NE(playScene, nullptr);
    ASSERT_TRUE(sceneManager->activateScene(playScene));

    EXPECT_EQ(sceneManager->getActiveScene(), playScene.get());
    EXPECT_EQ(sceneManager->getSceneByRegistry(&authoringScene->getRegistry()), authoringScene.get());
    EXPECT_EQ(sceneManager->getSceneByRegistry(&playScene->getRegistry()), playScene.get());

    ASSERT_TRUE(sceneManager->activateScene(authoringScene));
    EXPECT_TRUE(sceneManager->destroyScene(playScene));
    EXPECT_EQ(playScene, nullptr);
    EXPECT_EQ(sceneManager->getActiveScene(), authoringScene.get());
    EXPECT_EQ(sceneManager->getSceneByRegistry(&authoringScene->getRegistry()), authoringScene.get());
}

TEST_F(AppLifecycleTest, ExtensionsDispatchInRegistrationOrderAndDetachInReverseOrder)
{
    std::vector<std::string> calls;
    app.addExtension(std::make_unique<RecordingExtension>(calls, "first"));
    app.addExtension(std::make_unique<RecordingExtension>(calls, "second", true));
    app.addExtension(std::make_unique<RecordingExtension>(calls, "third"));

    AppExtensionTestAccess::configure(app);
    AppExtensionTestAccess::attach(app);

    SDL_Event nativeEvent{};
    nativeEvent.type = SDL_EVENT_FIRST;
    AppExtensionTestAccess::dispatchNativeEvent(app, nativeEvent);

    AppQuitEvent event;
    EXPECT_TRUE(AppExtensionTestAccess::dispatchEvent(app, event));
    AppExtensionTestAccess::tick(app, 0.016f);
    AppExtensionTestAccess::prepareRender(app, 0.016f);

    ASSERT_TRUE(sceneManager->activateScene(makeShared<Scene>("Runtime")));
    app.startSimulation();
    app.stopSimulation();
    AppExtensionTestAccess::detach(app);

    EXPECT_EQ(calls,
              (std::vector<std::string>{
                  "first.configure", "second.configure", "third.configure",
                  "first.attach", "second.attach", "third.attach",
                  "first.native", "second.native", "third.native",
                  "first.event", "second.event",
                  "first.logic", "second.logic", "third.logic",
                  "first.before-render", "second.before-render", "third.before-render",
                  "first.before-state", "second.before-state", "third.before-state",
                  "first.after-state", "second.after-state", "third.after-state",
                  "first.before-state", "second.before-state", "third.before-state",
                  "first.after-state", "second.after-state", "third.after-state",
                  "third.detach", "second.detach", "first.detach",
              }));
}

TEST_F(AppLifecycleTest, ExtensionDispatchIsSafeWithoutExtensions)
{
    SDL_Event nativeEvent{};
    nativeEvent.type = SDL_EVENT_FIRST;
    AppQuitEvent event;

    AppExtensionTestAccess::configure(app);
    AppExtensionTestAccess::attach(app);
    AppExtensionTestAccess::dispatchNativeEvent(app, nativeEvent);
    EXPECT_FALSE(AppExtensionTestAccess::dispatchEvent(app, event));
    AppExtensionTestAccess::tick(app, 0.016f);
    AppExtensionTestAccess::prepareRender(app, 0.016f);
    AppExtensionTestAccess::detach(app);
}

} // namespace
} // namespace ya
