#pragma once

// ============================================================================
// GUIAppHost - standalone native GUI app host (gui-app-bootstrap Phase 1).
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

#include "Core/Event.h"

#include "Core/Application/AutomationRun.h"
#include "GUI/Widgets/WidgetTree.h"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace ya
{

class GUIAppHost;

/// Contract implemented by a standalone GUI app. The delegate owns the tree
/// content and the app state; the host owns the window and presentation.
struct IGUIAppDelegate
{
    virtual ~IGUIAppDelegate() = default;

    /// Mount the app's widgets. Called once right after the tree and render
    /// resources exist, before the first frame.
    virtual void buildUI(WidgetTree& tree) = 0;

    /// Called every frame right before buildSnapshot: sync presentation state
    /// (labels / selection / dirty layout) from the app state. All widget
    /// mutation happens here or in event callbacks, never during command
    /// recording.
    virtual void updateUI() {}

    /// Optional observation of a routed UI event (smoke logs / diagnostics).
    virtual void onRoutedEvent(const Event& /*event*/, EWidgetRouteResult /*result*/) {}

    /// App-driven graceful shutdown request observed by the host at frame
    /// boundaries. Lets tool automation finish on its own terminal state
    /// instead of relying on an external frame budget.
    [[nodiscard]] virtual bool shouldRequestClose() const { return false; }
};

struct FGUIAppHostConfig
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
    /// Debug: capture the swapchain image on `gpuShotFrame` (GPU readback)
    /// and write it as a BMP. This validates the real presentation output
    /// (orientation, fonts, compositing) — the CPU dump cannot. 0 disables.
    std::string              gpuShotPath;
    uint32_t                 gpuShotFrame = 0;
    /// Scenario harness: when non-empty, run() drives the tree from a JSONL
    /// scenario (via GuiEventDriver) instead of the SDL event pump. Scenario
    /// scripts end with a frame step so the final state is rendered and
    /// (optionally) captured + diffed against scenarioGoldenPath.
    std::string              scenarioPath;
    std::string              scenarioDumpDir;
    std::string              scenarioCapturePath;
    std::string              scenarioGoldenPath;
    std::string              scenarioDiffPath;
    std::vector<uint32_t>    fontSizes{16, 20};
    /// Whether Escape (and SDL_QUIT) stops the app loop. Host-level key
    /// handling; app widgets never see Escape while this is enabled.
    bool bEscapeQuits = true;
    /// Shared automation run policy. Zero means "run until closed".
    AppAutomationRunOptions automation;
};

/// Standalone GUI app host. Create, init(), run(), shutdown().
/// The delegate must outlive the host.
class GUIAppHost
{
public:
    GUIAppHost(const FGUIAppHostConfig& config, IGUIAppDelegate& delegate);
    ~GUIAppHost();

    GUIAppHost(const GUIAppHost&)            = delete;
    GUIAppHost& operator=(const GUIAppHost&) = delete;

    /// Create the window / backend / presentation resources and mount the
    /// delegate content. Returns false on any init failure.
    [[nodiscard]] bool init();
    /// Run the frame loop until quit (SDL_QUIT / Escape / requestClose()) or
    /// the shared automation policy asks for a graceful stop.
    [[nodiscard]] int run();
    /// Tear down in reverse order; idempotent (safe to call even after a
    /// failed init).
    void shutdown();

    /// Live UI tree owned by the host (valid after init()).
    [[nodiscard]] WidgetTree& getTree();
    /// Inject one event through the same path SDL uses (scenario driver +
    /// automation inject_event command).
    void injectEvent(const Event& event, const glm::vec2& logicalPoint);

private:
    void pumpEvents(bool& bRunning);
    void dispatchToTree(const Event& event, float mouseX, float mouseY);
    /// Drive scenario events until the next Frame step (or scenario end).
    void stepScenario(bool& bRunning);
    void rebuildPresentationResources(bool bWaitForGpu = true);

    struct FImpl;
    std::unique_ptr<FImpl> _impl;
};

} // namespace ya
