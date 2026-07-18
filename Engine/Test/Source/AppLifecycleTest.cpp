#include "Runtime/App/Lifecycle/AppLifecycle.h"

#include "Runtime/App/App.h"

#include "Scene/SceneManager.h"

#include <gtest/gtest.h>

namespace ya
{
namespace
{

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

} // namespace
} // namespace ya
