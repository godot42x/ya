#include "AppModuleTestAccess.h"

#include "GameRuntime/App.h"
#include "GameRuntime/IRuntimeModule.h"

#include "Core/System/VirtualFileSystem.h"
#include "RHI/Core/CommandBuffer.h"
#include "RHI/Core/RenderSurfaceContext.h"
#include "Scene/Runtime/SceneManager.h"

#include <gtest/gtest.h>

#include <filesystem>
#include <string>
#include <utility>
#include <vector>

namespace ya
{
namespace
{

class PresentationTestCommandBuffer final : public ICommandBuffer
{
  public:
    CommandBufferHandle getHandle() const override { return {}; }
    CommandBufferHandle getTypedHandle() const override { return {}; }
    bool begin(bool = false) override { return true; }
    bool end() override { return true; }
    void reset() override { clearRetiredResources(); }
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

struct RecordingAppModule final : IModule, IRuntimeModule
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
    void* queryInterface(FInterfaceId interfaceId) override
    {
        return interfaceId == YA_RUNTIME_MODULE_INTERFACE ? static_cast<IRuntimeModule*>(this) : nullptr;
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
    bool onEvent(App&, const Event&) override
    {
        calls.push_back(name + ".event");
        return consumesEvents;
    }
    void onLogic(App&, float) override { calls.push_back(name + ".logic"); }
    void onBeforeRender(App&, float) override { calls.push_back(name + ".before-render"); }
    void onPresentation(App&, ICommandBuffer&, float) override { calls.push_back(name + ".presentation"); }
};

struct CountingEventModule final : IModule, IRuntimeModule
{
    int inputEvents    = 0;
    int nonInputEvents = 0;

    bool onLoad(FModuleContext&) override { return true; }
    bool onStart(const FEngineContext&) override { return true; }
    void onStop() override {}
    void onUnload() override {}
    void* queryInterface(FInterfaceId interfaceId) override
    {
        return interfaceId == YA_RUNTIME_MODULE_INTERFACE ? static_cast<IRuntimeModule*>(this) : nullptr;
    }
    bool onEvent(App&, const Event& event) override
    {
        if (event.isInCategory(EEventCategory::Input)) {
            ++inputEvents;
        }
        else {
            ++nonInputEvents;
        }
        return false;
    }
};

/// A module that draws the whole surface itself (the editor's chrome does).
struct SurfaceFillingModule final : IModule, IRuntimeModule
{
    bool bFills = true;

    bool onLoad(FModuleContext&) override { return true; }
    bool onStart(const FEngineContext&) override { return true; }
    void onStop() override {}
    void onUnload() override {}
    void* queryInterface(FInterfaceId interfaceId) override
    {
        return interfaceId == YA_RUNTIME_MODULE_INTERFACE ? static_cast<IRuntimeModule*>(this) : nullptr;
    }
    [[nodiscard]] bool fillsSurface(const IRenderSurfaceContext&) const override { return bFills; }
};

/// A window the backdrop policy can be asked about. The question is which
/// surface a module fills, so the policy needs surface identity and nothing
/// else -- no device, no swapchain.
struct StandInSurface final : IRenderSurfaceContext
{
    [[nodiscard]] INativeWindow* getNativeWindow() const override { return nullptr; }
    [[nodiscard]] ISwapchain*    getSwapchain() const override { return nullptr; }
    bool buildPresentationImages(IRenderResourceFactory&,
                                 const char*,
                                 std::vector<std::shared_ptr<RenderTexture>>&) override
    {
        return false;
    }
    [[nodiscard]] bool isPresentable() const override { return true; }
    void               requestRecreate() override {}
    bool begin(int32_t* imageIndex) override
    {
        *imageIndex = -1;
        return true;
    }
    bool end(int32_t, std::vector<void*>) override { return true; }
    void waitInFlight() override {}
    [[nodiscard]] void* getCurrentImageAvailableSemaphore() override { return nullptr; }
    [[nodiscard]] void* getCurrentFrameFence() override { return nullptr; }
    [[nodiscard]] void* getRenderFinishedSemaphore(uint32_t) override { return nullptr; }
};

class AppLifecycleTest : public ::testing::Test
{
  protected:
    App app;
    std::unique_ptr<SceneManager> sceneManager;

    void SetUp() override
    {
        VirtualFileSystem::init();
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

TEST_F(AppLifecycleTest, ResolveStartupScenePrefersAutomationOverride)
{
    AppDesc desc;
    desc.defaultScenePath    = "Content/Scenes/default.scene.json";
    desc.automation.scenePath = "Content/Scenes/automation.scene.json";

    EXPECT_EQ(AppModuleTestAccess::resolveStartupScenePath(desc), "Content/Scenes/automation.scene.json");
}

TEST_F(AppLifecycleTest, ResolveStartupSceneFallsBackToDefaultScene)
{
    AppDesc desc;
    desc.defaultScenePath = "Content/Scenes/default.scene.json";

    EXPECT_EQ(AppModuleTestAccess::resolveStartupScenePath(desc), "Content/Scenes/default.scene.json");
}

TEST_F(AppLifecycleTest, LoadSceneIgnoresEmptyPathWithoutCreatingFallbackScene)
{
    EXPECT_FALSE(sceneManager->hasScene());

    const bool bLoaded = AppModuleTestAccess::loadScene(app, "");

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

TEST_F(AppLifecycleTest, TheSurfaceBackdropIsWhatTheLoadedModulesSayItIs)
{
    /// A host with no surface-filling module shows the View: this is the
    /// standalone runtime, where display compose must copy the View across the
    /// window.
    StandInSurface surface;

    EXPECT_TRUE(app.presentsViewDisplayImage(surface));

    auto filler = std::make_unique<SurfaceFillingModule>();
    SurfaceFillingModule* fillerModule = filler.get();
    app.addModule(std::move(filler));
    AppModuleTestAccess::configure(app);
    AppModuleTestAccess::attach(app);

    /// One module filling the surface is enough: the View copy would be
    /// overdrawn, so display compose must not make it. The editor is this case
    /// in every state it is loaded in, including a play session.
    EXPECT_FALSE(app.presentsViewDisplayImage(surface));

    /// The answer follows the module, not a registration-time decision.
    fillerModule->bFills = false;
    EXPECT_TRUE(app.presentsViewDisplayImage(surface));
}

TEST_F(AppLifecycleTest, ModulesDispatchInRegistrationOrderAndDetachInReverseOrder)
{
    std::vector<std::string> calls;
    app.addModule(std::make_unique<RecordingAppModule>(calls, "first"));
    app.addModule(std::make_unique<RecordingAppModule>(calls, "second", true));
    app.addModule(std::make_unique<RecordingAppModule>(calls, "third"));

    AppModuleTestAccess::configure(app);
    AppModuleTestAccess::attach(app);

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
    AppQuitEvent event;

    AppModuleTestAccess::configure(app);
    AppModuleTestAccess::attach(app);
    EXPECT_FALSE(AppModuleTestAccess::dispatchEvent(app, event));
    AppModuleTestAccess::tick(app, 0.016f);
    AppModuleTestAccess::prepareRender(app, 0.016f);
    PresentationTestCommandBuffer commandBuffer;
    AppModuleTestAccess::recordPresentation(app, commandBuffer, 0.016f);
    AppModuleTestAccess::detach(app);
}

TEST_F(AppLifecycleTest, InputEventsReachModulesOnlyThroughInputRouter)
{
    auto module = std::make_unique<CountingEventModule>();
    auto* raw   = module.get();
    app.addModule(std::move(module));

    AppModuleTestAccess::configure(app);
    AppModuleTestAccess::attach(app);

    MouseButtonPressedEvent mousePressed(EMouse::Left);
    EXPECT_EQ(app.onEvent(mousePressed), 0);
    EXPECT_EQ(raw->inputEvents, 1);
    EXPECT_EQ(raw->nonInputEvents, 0);

    WindowResizeEvent resizeEvent(1, 1280, 720);
    EXPECT_EQ(app.onEvent(resizeEvent), 0);
    EXPECT_EQ(raw->inputEvents, 1);
    EXPECT_EQ(raw->nonInputEvents, 1);

    AppModuleTestAccess::detach(app);
}

TEST_F(AppLifecycleTest, SaveScenePersistsAndReloadsRoundTrip)
{
    auto scene = makeShared<Scene>("RoundTripScene");
    ASSERT_TRUE(sceneManager->activateScene(scene));
    ASSERT_NE(scene->createNode("Cube"), nullptr);

    const auto savePath = std::filesystem::temp_directory_path() /
                          ("ya-scene-roundtrip-" + std::to_string(::testing::UnitTest::GetInstance()->random_seed()) + ".scene.json");
    std::error_code removeError;
    std::filesystem::remove(savePath, removeError);

    ASSERT_TRUE(app.getSceneServices().saveScene(savePath.string()));
    ASSERT_TRUE(std::filesystem::is_regular_file(savePath));

    SceneManager reloadManager;
    ASSERT_TRUE(reloadManager.loadScene(savePath.string()));
    ASSERT_NE(reloadManager.getActiveScene(), nullptr);
    EXPECT_EQ(reloadManager.getActiveScene()->getName(), "RoundTripScene");

    std::filesystem::remove(savePath, removeError);
}

} // namespace
} // namespace ya
