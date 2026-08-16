#pragma once

// ============================================================================
// GUIWindowHost - standalone native GUI window host.
//
// Owns the full presentation lifecycle of a GUI-only binary:
//   SDL window / shader storage / IRender / builtin textures / fonts /
//   Render2D / imported swapchain presentation targets / command buffers.
//
// Frame contract (single-threaded, frame-boundary only):
//   SDL events -> WidgetTree::dispatchEvent (tree-local logical points)
//   begin -> swapchain-stability check -> prepare compose pipeline
//         -> delegate.updateUI() -> WidgetTree::buildSnapshot
//         -> record clear + compose -> end/present
//
// The host never knows Scene / ECS / Render3D / Product Host / Editor /
// ToolWorkspace; delegates implement IGUIAppDelegate to mount widgets and
// sync presentation state. GPU resources are rebuilt only at frame
// boundaries (rebuildPresentationResources), never during event dispatch or
// command recording.
//
// The public surface exposes no command buffer / swapchain image / Vulkan
// type: app delegates interact with the WidgetTree and the frame contract
// only.
// ============================================================================

#include "Core/Api.h"

#include "App/Kernel/AppKernel.h"
#include "App/Control/AutomationRun.h"
#include "GUI/Host/GUIAppDelegate.h"

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace ya
{

struct FGUIWindowHostConfig
{
    std::string title      = "YA GUI App";
    uint32_t    width      = 1024;
    uint32_t    height     = 768;
    float       scale      = 1.0f;
    bool        bResizable = true;
    /// Present with vertical sync (FIFO). Disabling it selects Immediate
    /// mode: without vsync the direct swapchain presentation tears / 
    /// flickers on most displays, so GUI apps should keep this enabled.
    bool        bVsync     = true;
    /// Runtime font: loaded once per entry under DEFAULT_RUNTIME_FONT_NAME
    /// (UIText resolves fonts by exact name+size). Empty to skip font loading.
    std::string              fontPath = "Engine/Content/Fonts/JetBrainsMono-Medium.ttf";
    /// Debug: dump the first UI snapshot as a BMP (CPU-side raster of the
    /// draw items, top-left origin). Empty to disable; dumpFrame selects the
    /// frame (0 = the first snapshot).
    std::string              dumpSnapshotPath;
    uint32_t                 dumpFrame = 0;
    /// Structural frame packet dump for cross-path automation comparison.
    /// Resource pointers are excluded; use alongside the GPU shot when a
    /// visual diff is also required.
    std::string              dumpSnapshotJsonPath;
    /// Debug: capture the swapchain image on `gpuShotFrame` (GPU readback)
    /// and write it as a BMP. This validates the real presentation output
    /// (orientation, fonts, compositing) — the CPU dump cannot. 0 disables.
    std::string              gpuShotPath;
    uint32_t                 gpuShotFrame = 0;
    /// Render the same immutable snapshot to a Framework-owned offscreen
    /// GUIRenderSurface and capture it. Together with gpuShotPath this proves
    /// windowed/offscreen compose parity for one frame.
    std::string              offscreenShotPath;
    uint32_t                 offscreenShotFrame = 0;
    /// Optional zero-tolerance BMP diff written after run() when both a
    /// windowed GPU shot and offscreen shot were requested.
    std::string              offscreenDiffPath;
    /// Scenario harness: when non-empty, run() drives the tree from a JSONL
    /// scenario (via GuiEventDriver) instead of the SDL event pump. Scenario
    /// scripts end with a frame step so the final state is rendered and
    /// (optionally) captured + diffed against scenarioGoldenPath.
    std::string              scenarioPath;
    std::string              scenarioDumpDir;
    std::string              scenarioCapturePath;
    std::string              scenarioGoldenPath;
    std::string              scenarioDiffPath;
    /// Draw a host-injected snapshot overlay showing render bounds, clip,
    /// pointer/focus route paths, capture and hover. Used to debug coordinate,
    /// clipping and event routing without touching app widgets or Render2D
    /// internals.
    bool                     bDebugRenderOverlay = false;
    std::vector<uint32_t>    fontSizes{16, 20};
    /// Whether Escape (and SDL_QUIT) stops the app loop. Host-level key
    /// handling; app widgets never see Escape while this is enabled.
    bool bEscapeQuits = true;
    /// Shared automation run policy. Zero means "run until closed".
    AppAutomationRunOptions automation;
};

/// One native GUI window: owns its SDL window, presentation resources,
/// transient pointer state and exactly one WidgetTree. It is the concrete
/// single-window owner; GUIAppHost remains only as a legacy type alias.
/// The delegate must outlive the host.
class YA_GUI_API GUIWindowHost : public IAppLoopDelegate
{
public:
    GUIWindowHost(const FGUIWindowHostConfig& config, IGUIAppDelegate& delegate);
    ~GUIWindowHost();

    GUIWindowHost(const GUIWindowHost&)            = delete;
    GUIWindowHost& operator=(const GUIWindowHost&) = delete;

    /// Create the window / backend / presentation resources and mount the
    /// delegate content. Returns false on any init failure.
    [[nodiscard]] bool init();
    /// Run the frame loop until quit (SDL_QUIT / Escape / requestClose()) or
    /// the shared automation policy asks for a graceful stop. Transitional
    /// convenience only: new code should let GUIApp own AppKernel.
    [[nodiscard]] int run();
    /// Tear down in reverse order; idempotent (safe to call even after a
    /// failed init).
    void shutdown();

    /// Live UI tree owned by the host (valid after init()).
    [[nodiscard]] WidgetTree& getTree();
    /// Inject one event through the same path SDL uses (scenario driver +
    /// automation inject_event command).
    void injectEvent(const Event& event, const glm::vec2& logicalPoint);
    [[nodiscard]] bool isInitialized() const;
    [[nodiscard]] IAppEventSource* getEventSource();
    [[nodiscard]] const FGUIWindowHostConfig& getConfig() const;
    /// Complete scenario / surface parity diffs after an externally-owned
    /// AppKernel run. GUIApp calls this after its kernel exits.
    [[nodiscard]] int finishRun(int kernelResult);

    // === IAppLoopDelegate (driven by AppKernel; init/shutdown stay public) ===
    void onInit() override;
    void onEvent(const Event& event) override;
    void onTick(float dt) override;
    void onShutdown() override;
    [[nodiscard]] bool shouldClose() const override;

private:
    void dispatchToTree(const Event& event, float mouseX, float mouseY);
    [[nodiscard]] bool requestWindowSize(uint32_t width, uint32_t height, std::string_view reason);
    /// Write a scenario checkpoint tree dump (<scenarioDumpDir>/<tag>.json).
    void dumpScenarioCheckpoint(const std::string& tag);
    void rebuildPresentationResources(bool bWaitForGpu = true);
    /// Apply the hovered widget's requested cursor (system cursor, deduped).
    void updateCursor();

    struct FImpl;
    std::unique_ptr<FImpl> _impl;
};

/// GUI assembly/policy layer. v1 owns one primary window but it is deliberately
/// a composition of GUIWindowHost rather than a second event/render loop; the
/// next multi-window increment grows a registry here.
class YA_GUI_API GUIApp final
{
public:
    GUIApp(const FGUIWindowHostConfig& config, IGUIAppDelegate& delegate);

    GUIApp(const GUIApp&)            = delete;
    GUIApp& operator=(const GUIApp&) = delete;

    [[nodiscard]] bool init();
    [[nodiscard]] int  run();
    void               shutdown();

    [[nodiscard]] GUIWindowHost& getPrimaryWindow() { return _primaryWindow; }
    [[nodiscard]] WidgetTree&    getTree() { return _primaryWindow.getTree(); }
    void injectEvent(const Event& event, const glm::vec2& logicalPoint)
    {
        _primaryWindow.injectEvent(event, logicalPoint);
    }

private:
    GUIWindowHost _primaryWindow;
};

/// Legacy names kept for existing examples. New framework code should use
/// GUIApp for the app assembly and GUIWindowHost for the concrete
/// one-window owner.
using FGUIAppHostConfig = FGUIWindowHostConfig;
using GUIAppHost         = GUIApp;

} // namespace ya
