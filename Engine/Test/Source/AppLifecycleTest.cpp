#include "Runtime/Application/Lifecycle/AppLifecycle.h"

#include "Runtime/Application/App.h"

#include "Render/Core/CommandBuffer.h"
#include "Scene/SceneManager.h"

#include <gtest/gtest.h>

#include <string>
#include <utility>
#include <vector>

namespace ya
{

class AppModuleTestAccess
{
  public:
    static void configure(App& app) { app.configureModules(); }
    static void attach(App& app) { app.attachModules(); }
    static void detach(App& app) { app.detachModules(); }
    static void setSceneManager(App& app, SceneManager* sceneManager) { app._sceneManager = sceneManager; }
    static void setAppState(App& app, AppState state) { app._appState = state; }
    static void dispatchNativeEvent(App& app, const SDL_Event& event) { app.dispatchNativeEvent(event); }
    static bool dispatchEvent(App& app, const Event& event) { return app.dispatchModuleEvent(event); }
    static void tick(App& app, float dt) { app.tickModules(dt); }
    static void prepareRender(App& app, float dt) { app.prepareModulesForRender(dt); }
    static void recordPresentation(App& app, ICommandBuffer& commandBuffer, float dt) { app.recordModulePresentation(commandBuffer, dt); }
};

namespace
{

class PresentationTestCommandBuffer final : public ICommandBuffer
{
  public:
    CommandBufferHandle getHandle() const override { return {}; }
    CommandBufferHandle getTypedHandle() const override { return {}; }
    bool begin(bool = false) override { return true; }
    bool end() override { return true; }
    void reset() override { clearRetainedResources(); }
    void bindPipeline(IGraphicsPipeline*) override {}
    void bindComputePipeline(IComputePipeline*) override {}
    void bindVertexBuffer(uint32_t, const IBuffer*, uint64_t = 0) override {}
    void bindIndexBuffer(IBuffer*, uint64_t = 0, bool = false) override {}
    void draw(uint32_t, uint32_t = 1, uint32_t = 0, uint32_t = 0) override {}
    void drawIndexed(uint32_t, uint32_t = 1, uint32_t = 0, int32_t = 0, uint32_t = 0) override {}
    void setViewport(float, float, float, float, float = 0.0f, float = 1.0f) override {}
    void setScissor(int32_t, int32_t, uint32_t, uint32_t) override {}
    void setCullMode(ECullMode::T) override {}
    void setPolygonMode(EPolygonMode::T) override {}
    void setDepthBias(float, float, float) override {}
    void bindDescriptorSets(IPipelineLayout*, uint32_t, const std::vector<DescriptorSetHandle>&, const std::vector<uint32_t>& = {}) override {}
    void bindComputeDescriptorSets(IPipelineLayout*, uint32_t, const std::vector<DescriptorSetHandle>&, const std::vector<uint32_t>& = {}) override {}
    void pushConstants(IPipelineLayout*, EShaderStage::T, uint32_t, uint32_t, const void*) override {}
    void copyBuffer(IBuffer*, IBuffer*, uint64_t, uint64_t = 0, uint64_t = 0) override {}
    void dispatch(uint32_t, uint32_t, uint32_t) override {}
    void dispatchIndirect(IBuffer*, uint64_t = 0) override {}
    void drawIndirect(IBuffer*, uint64_t, uint32_t, uint32_t) override {}
    void drawIndexedIndirect(IBuffer*, uint64_t, uint32_t, uint32_t) override {}
    void drawIndexedIndirectCount(IBuffer*, uint64_t, IBuffer*, uint64_t, uint32_t, uint32_t) override {}
    void fillBuffer(IBuffer*, uint64_t, uint64_t, uint32_t) override {}
    void bufferMemoryBarrier(IBuffer*, EPipelineStage::T, EPipelineStage::T, EResourceAccess::T, EResourceAccess::T, uint64_t = 0, uint64_t = 0) override {}
    void copyBufferToImage(IBuffer*, IImage*, EImageLayout::T, const std::vector<BufferImageCopy>&) override {}
    void copyImageToBuffer(IImage*, EImageLayout::T, IBuffer*, const std::vector<BufferImageCopy>&) override {}
    void copyImage(IImage*, EImageLayout::T, IImage*, EImageLayout::T, const std::vector<ImageCopy>&) override {}
    void beginRendering(const RenderingInfo&) override {}
    void endRendering(const RenderingInfo& = {}) override {}
    void transitionImageLayout(IImage*, EImageLayout::T, EImageLayout::T, const ImageSubresourceRange* = nullptr) override {}
    void transitionImageLayoutAuto(IImage*, EImageLayout::T, const ImageSubresourceRange* = nullptr) override {}
    void debugBeginLabel(const char*, const float* = nullptr) override {}
    void debugEndLabel() override {}
};

struct RecordingAppModule final : IModule
{
    std::vector<std::string>& calls;
    std::string               name;
    bool                      consumesEvents = false;

    RecordingAppModule(std::vector<std::string>& calls, std::string name, bool consumesEvents = false)
        : calls(calls), name(std::move(name)), consumesEvents(consumesEvents)
    {
    }

    bool onLoad(FModuleContext&) override { return true; }
    bool onStart(const FEngineContext&) override { return true; }
    void onStop() override {}
    void onUnload() override {}
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
    void onPresentation(App&, ICommandBuffer&, float) override { calls.push_back(name + ".presentation"); }
};

class AppLifecycleTest : public ::testing::Test
{
  protected:
    App app;
    std::unique_ptr<SceneManager> sceneManager;

    void SetUp() override
    {
        sceneManager = std::make_unique<SceneManager>();
        AppModuleTestAccess::setSceneManager(app, sceneManager.get());
        AppModuleTestAccess::setAppState(app, AppState::Stopped);
    }

    void TearDown() override
    {
        AppModuleTestAccess::setSceneManager(app, nullptr);
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

TEST_F(AppLifecycleTest, ModulesDispatchInRegistrationOrderAndDetachInReverseOrder)
{
    std::vector<std::string> calls;
    app.addModule(std::make_unique<RecordingAppModule>(calls, "first"));
    app.addModule(std::make_unique<RecordingAppModule>(calls, "second", true));
    app.addModule(std::make_unique<RecordingAppModule>(calls, "third"));

    AppModuleTestAccess::configure(app);
    AppModuleTestAccess::attach(app);

    SDL_Event nativeEvent{};
    nativeEvent.type = SDL_EVENT_FIRST;
    AppModuleTestAccess::dispatchNativeEvent(app, nativeEvent);

    AppQuitEvent event;
    EXPECT_TRUE(AppModuleTestAccess::dispatchEvent(app, event));
    AppModuleTestAccess::tick(app, 0.016f);
    AppModuleTestAccess::prepareRender(app, 0.016f);
    PresentationTestCommandBuffer commandBuffer;
    AppModuleTestAccess::recordPresentation(app, commandBuffer, 0.016f);

    ASSERT_TRUE(sceneManager->activateScene(makeShared<Scene>("Runtime")));
    app.startSimulation();
    app.stopSimulation();
    AppModuleTestAccess::detach(app);

    EXPECT_EQ(calls,
              (std::vector<std::string>{
                  "first.configure", "second.configure", "third.configure",
                  "first.attach", "second.attach", "third.attach",
                  "first.native", "second.native", "third.native",
                  "first.event", "second.event",
                  "first.logic", "second.logic", "third.logic",
                  "first.before-render", "second.before-render", "third.before-render",
                  "first.presentation", "second.presentation", "third.presentation",
                  "first.before-state", "second.before-state", "third.before-state",
                  "first.after-state", "second.after-state", "third.after-state",
                  "first.before-state", "second.before-state", "third.before-state",
                  "first.after-state", "second.after-state", "third.after-state",
                  "third.detach", "second.detach", "first.detach",
              }));
}

TEST_F(AppLifecycleTest, ModuleDispatchIsSafeWithoutModules)
{
    SDL_Event nativeEvent{};
    nativeEvent.type = SDL_EVENT_FIRST;
    AppQuitEvent event;

    AppModuleTestAccess::configure(app);
    AppModuleTestAccess::attach(app);
    AppModuleTestAccess::dispatchNativeEvent(app, nativeEvent);
    EXPECT_FALSE(AppModuleTestAccess::dispatchEvent(app, event));
    AppModuleTestAccess::tick(app, 0.016f);
    AppModuleTestAccess::prepareRender(app, 0.016f);
    PresentationTestCommandBuffer commandBuffer;
    AppModuleTestAccess::recordPresentation(app, commandBuffer, 0.016f);
    AppModuleTestAccess::detach(app);
}

} // namespace
} // namespace ya
