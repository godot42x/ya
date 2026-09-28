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
//   updateUI + buildSnapshot (also when unpresentable)
//   if presentable: begin -> swapchain-stability check -> prepare compose
//         -> record clear + compose -> end/present
//   Unpresentable (minimized / zero extent) skips acquire/present only.
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
#include "App/Control/AutomationControlServer.h"
#include "App/Control/AutomationRun.h"
#include "GUI/Host/GUIAppDelegate.h"
#include "GUI/Host/GUIDragRouter.h"
#include "GUI/Host/GUIWindowChrome.h"
#include "GUI/Host/IGUIWindowSession.h"
#include "RHI/Core/PresentFrame.h"
#include "RHI/RenderDefines.h"

#include <cstdint>
#include <cstddef>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace ya
{

struct IRender; // forward decl: getRender() returns it, no definition needed here
struct ScreenDrawPipelines;
struct INativeWindow;
struct FGUIPresentExtensionContext;
struct RenderTexture;

struct FGUIWindowHostConfig
{
    std::string title      = "YA GUI App";
    uint32_t    width      = 1024;
    uint32_t    height     = 768;
    int         posX       = 0;
    int         posY       = 0;
    int         monitorIndex = -1;
    bool        bHasPosition = false;
    bool        bMaximized = false;
    float       scale      = 1.0f;
    bool        bResizable = true;
    /// Present with vertical sync (FIFO). Disabling it selects Immediate
    /// mode: without vsync the direct swapchain presentation tears / 
    /// flickers on most displays, so GUI apps should keep this enabled.
    bool        bVsync     = true;
    /// Native window + IRender backend. Extra windows follow the live
    /// `IRender::getAPI()` when a device already exists; otherwise this value.
    ERenderAPI::T            renderAPI = ERenderAPI::Vulkan;
    /// Which UI face the host loads under DEFAULT_RUNTIME_FONT_NAME, by catalog
    /// id (FontManager::uiFontFaces). Empty skips font loading entirely.
    ///
    /// An ID rather than a path because the face is a durable CHOICE: the host
    /// builds the whole stack (primary + CJK/emoji fallbacks + the monospace
    /// family) from it, and a caller that can only pass a path cannot express
    /// "the engine's default face" without re-deriving it. Inter (OFL, bundled)
    /// is the default face in the catalog because chrome typography needs a
    /// PROPORTIONAL UI face - a monospace primary makes every label, menu and
    /// field read as terminal output - and it is bundled so text metrics stay
    /// identical across macOS/Windows.
    ///
    /// Applications that expose a font setting should not read this struct:
    /// resolve the user's choice once (see GameRuntime/Settings/UiFontSettings.h)
    /// and pass the id here.
    std::string              uiFontFace = "inter";
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
    /// Guardrail G-C: render full frames during scenario replay even while
    /// the window is minimized, so the G2 validation frame runs inside
    /// behavior scenarios (assert_validation_clean can catch missed dirty
    /// marks before manual testing).
    bool                     bScenarioRender = false;
    /// Draw a host-injected snapshot overlay showing render bounds, clip,
    /// pointer/focus route paths, capture and hover. Used to debug coordinate,
    /// clipping and event routing without touching app widgets or Render2D
    /// internals.
    bool                     bDebugRenderOverlay = false;
    /// Frame inspector spec (`hud`, `rebuild`, `overdraw`). Compiled out of
    /// release/releasedbg; host WARNs and ignores the flag in those builds.
    std::string              guiFrameInspector;
    std::vector<uint32_t>    fontSizes{16, 20};
    /// Whether Escape (and the process quit event) stops the app loop. Host-level key
    /// handling; app widgets never see Escape while this is enabled.
    bool bEscapeQuits = true;
    /// Empty = platform default (macOS Hybrid, elsewhere Native).
    std::optional<EWindowChromeMode> chromeMode;
    /// Host-owned drag-ghost overlay: click-through, no taskbar, not an editor session.
    bool bDragOverlay = false;
    /// Window-appearance flags for non-overlay sessions (launch splash etc.).
    /// `bDragOverlay` implies all of them plus click-through; these are opt-in
    /// per flag and compose with the resolved chrome mode.
    bool bBorderless   = false;
    bool bAlwaysOnTop  = false;
    bool bTransparent  = false;
    bool bNotFocusable = false;
    bool bUtility      = false;
    bool bMousePassthrough = false;
    /// Shared automation run policy. Zero means "run until closed".
    AppAutomationRunOptions automation;
};

/// How one GUI host window becomes a presentable surface: the single place a
/// window's configuration turns into a swapchain description.
///
/// Both windows the device is created for and windows opened later go through
/// this, so "which format, how many images, which present mode" has one answer
/// instead of one per registration path. The window's own size is the swapchain
/// extent; a resize recreates the swapchain rather than editing this.
[[nodiscard]] YA_GUI_API SwapchainCreateInfo makeHostWindowSurfaceDesc(const FGUIWindowHostConfig& config);

/// One native GUI window: owns its SDL window, presentation resources,
/// transient pointer state and exactly one WidgetTree.
///
/// It is also an `IGUIWindowSession`, so a registry of windows can hold it next
/// to the ones a GUI app opens later: the startup window has no special shape,
/// only a special creation time (the device is created for it).
/// The delegate must outlive the host.
class YA_GUI_API GUIWindowHost : public IAppLoopDelegate, public IGUIWindowSession
{
public:
    GUIWindowHost(const FGUIWindowHostConfig& config, IGUIAppDelegate& delegate);
    ~GUIWindowHost();

    GUIWindowHost(const GUIWindowHost&)            = delete;
    GUIWindowHost& operator=(const GUIWindowHost&) = delete;

    /// Create the window / backend / presentation resources and mount the
    /// delegate content. Returns false on any init failure.
    [[nodiscard]] bool init();
    /// Run the frame loop until quit (process quit / Escape / requestClose()) or
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
    [[nodiscard]] const FWindowChromeState&   windowChrome() const;
    [[nodiscard]] uint32_t getWindowID() const;
    [[nodiscard]] INativeWindow* getNativeWindow();
    [[nodiscard]] const INativeWindow* getNativeWindow() const;
    [[nodiscard]] IRender* getRender() const;
    /// Screen PSO cache for this window and any extra windows that present
    /// through the same device. Valid after a successful `init`.
    [[nodiscard]] ScreenDrawPipelines* screenDrawPipelines();
    /// When true, the SDL source emits events for every OS window so GUIApp
    /// can route extras. Default filters to this host's window.
    void setAcceptAllWindowEvents(bool enabled);
    /// Complete scenario / surface parity diffs after an externally-owned
    /// AppKernel run. GUIApp calls this after its kernel exits.
    [[nodiscard]] int finishRun(int kernelResult);

    /// === The tick, split at the point a registry of windows needs ===
    ///
    /// `tickContent` runs this window's live state one frame forward and
    /// publishes the snapshot that describes it; `presentSnapshot` puts the
    /// snapshot this window most recently built onto its surface. `onTick` is
    /// exactly those two, which is what a single-window caller wants.
    ///
    /// The split exists because an app with several windows must not interleave
    /// them: presenting window A before window B has ticked makes B's chrome a
    /// frame stale, and a surface that is torn off mid-frame would show a tree
    /// that was never laid out for it. Ticking every window first and then
    /// presenting every window keeps each frame's content and its presentation
    /// in the same frame for all of them.
    void tickContent(float dt);
    /// Record this window into `submission`. Does not submit or present.
    void recordSnapshot(FFrameSubmission& submission);
    /// Read back captures recorded by `recordSnapshot`, after the submission
    /// that contains this window has been submitted and presented.
    void completeCaptures();
    /// `recordSnapshot` + submit this window alone + `completeCaptures`.
    void presentSnapshot();
    /// The snapshot `tickContent` most recently published. Null before the first
    /// tick, which is the honest answer for "what does this window show" before
    /// it has been laid out once.
    [[nodiscard]] const UIFrameSnapshot* getSnapshot() const;

    // === IGUIWindowSession: this window in a registry of windows ===
    [[nodiscard]] GUIWindowId                    id() const override { return getWindowID(); }
    [[nodiscard]] INativeWindow*                 nativeWindow() const override;
    [[nodiscard]] WidgetTree*                    tree() const override;
    [[nodiscard]] const UIFrameSnapshot*         snapshot() const override { return getSnapshot(); }
    [[nodiscard]] IRenderSurfaceContext*         surfaceContext() const override;
    [[nodiscard]] bool                           isMinimized() const override;
    [[nodiscard]] bool                           closeRequested() const override;
    [[nodiscard]] const FWindowChromeState&      chrome() const override { return windowChrome(); }

    // === IAppLoopDelegate (driven by AppKernel; init/shutdown stay public) ===
    void onInit() override;
    void onEvent(const Event& event) override;
    void onTick(float dt) override;
    void onShutdown() override;
    [[nodiscard]] bool shouldClose() const override;

private:
    void dispatchToTree(const Event& event, float mouseX, float mouseY);
    [[nodiscard]] bool requestWindowSize(uint32_t width, uint32_t height, std::string_view reason);

    // === Automation control plane: framework-side verbs over one window ===
    /// Registers this host's methods on its registry (called by init once the
    /// control server is up; the frame loop only dispatches).
    void registerAutomationMethods();
    /// Consumes the control server's pending requests for this frame.
    void dispatchAutomationRequests();
    void onAutomationPing(const AppAutomationControlServer::RequestPtr& request);
    void onAutomationQuit(const AppAutomationControlServer::RequestPtr& request);
    void onAutomationDumpTree(const AppAutomationControlServer::RequestPtr& request);
    void onAutomationSetWindowSize(const AppAutomationControlServer::RequestPtr& request);
    void onAutomationMouseMove(const AppAutomationControlServer::RequestPtr& request);
    void onAutomationMousePress(const AppAutomationControlServer::RequestPtr& request);
    void onAutomationMouseRelease(const AppAutomationControlServer::RequestPtr& request);
    void onAutomationCaptureScreenshot(const AppAutomationControlServer::RequestPtr& request);

    /// Write a scenario checkpoint tree dump (<scenarioDumpDir>/<tag>.json).
    void dumpScenarioCheckpoint(const std::string& tag);
    /// This window's contribution to the shared present sequence
    /// (`recordGuiSnapshot`), recorded into the acquired image's command
    /// buffer before it is submitted.
    /// Offscreen parity mirror: the same snapshot onto an offscreen surface
    /// plus a GPU->CPU copy, so parity can diff "what the window shows" against
    /// "what the compose produced" at zero tolerance. Returns false when the
    /// mirror could not be created, in which case nothing was recorded.
    [[nodiscard]] bool recordOffscreenParityCapture(const FGUIPresentExtensionContext& ctx,
                                                    const UIFrameSnapshot&             snapshot,
                                                    std::shared_ptr<RenderTexture>&    outImage);
    /// GPU shot: copy the presented image itself into a readback buffer, still
    /// inside the same submission.
    void recordGpuShotCopy(const FGUIPresentExtensionContext& ctx);
    /// Apply the hovered widget's requested cursor (system cursor, deduped).
    void updateCursor();

    /// Acquire the system device-pixel-ratio (real HiDPI value from the
    /// window-system, not an extent ratio) and publish it through the shared
    /// DPI step (tree + font stack; the font stack alone before the tree
    /// exists). Called at startup and on every resize / monitor move. Returns
    /// the ratio.
    float refreshDevicePixelRatio();

    struct FImpl;
    std::unique_ptr<FImpl> _impl;
};

class GUIWindowManager;
class IGUIWindowCoordinator;
struct IGUIWindowSession;

/// GUI assembly/policy layer. Owns the startup `GUIWindowHost` plus the
/// native windows opened later (`GUIWindowManager` as `IGUIWindowCoordinator`).
/// One AppKernel drives all of them; every later window shares the process
/// device and does not call `IRender::create`.
///
/// Every window is a session, the startup one included: `findSession`,
/// `findTree`, `forEachSession` and `windowCount` answer for any window by id,
/// and one tick ticks every window's content before any window presents. The
/// startup window differs only in that the device was created for it (see
/// `RenderCreateInfo::startupSurfaces`).
class YA_GUI_API GUIApp final : public IAppLoopDelegate
{
    GUIWindowHost                     _primaryWindow;
    std::unique_ptr<GUIWindowManager> _extraWindows;
    GUIDragRouter                     _dragRouter;

public:
    GUIApp(const FGUIWindowHostConfig& config, IGUIAppDelegate& delegate);
    ~GUIApp();

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

    /// Extra OS window + WidgetTree on the shared device. Presents via renderAll.
    [[nodiscard]] GUIWindowId openWindow(const FGUIWindowHostConfig& config, IGUIAppDelegate& delegate);
    void                      closeWindow(GUIWindowId id);
    [[nodiscard]] WidgetTree* findTree(GUIWindowId id);
    [[nodiscard]] size_t      extraWindowCount() const;
    [[nodiscard]] IGUIWindowCoordinator& windowCoordinator();
    [[nodiscard]] IGUIWindowSession*     findSession(GUIWindowId id);
    /// Every window this app owns, the startup one first. A window that has not
    /// been initialized is not in the registry: `init()` is what creates one.
    [[nodiscard]] std::vector<IGUIWindowSession*> sessions() const;
    [[nodiscard]] size_t                          windowCount() const;
    /// Visit every window. Used by callers that treat windows uniformly (the
    /// tick, the drag router's window list) instead of naming the startup one.
    void forEachSession(const std::function<void(IGUIWindowSession&)>& fn) const;

    /// Unique drag session for this GUIApp (primary + extras). `GUIDragRouter`
    /// is the only source/hover identity; GUIApp methods are thin wrappers.
    [[nodiscard]] bool         isCrossWindowDragActive() const;
    [[nodiscard]] GUIWindowId  crossWindowDragSourceId() const;
    [[nodiscard]] GUIWindowId  crossWindowDragHoverId() const;
    [[nodiscard]] uint32_t     crossWindowDragEnterCount() const;
    [[nodiscard]] uint32_t     crossWindowDragLeaveCount() const;
    void                       runAfterDrag(std::function<void()> fn);

    void onInit() override;
    void onEvent(const Event& event) override;
    void onTick(float dt) override;
    void onShutdown() override;
    [[nodiscard]] bool shouldClose() const override;

private:
    void               bindDragRouter();
    void               adoptDragSource();
    void               syncCrossWindowDrag();
    void               applyPointerUniverse();
    [[nodiscard]] bool routeCrossWindowDrag(const Event& event);
    void               finishCrossWindowDrag(EDragFinishResult result);
    void               cancelCrossWindowDrag();
    void               applyDeferredCloses();
};

} // namespace ya
