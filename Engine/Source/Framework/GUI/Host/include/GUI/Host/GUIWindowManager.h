#pragma once

#include "Core/Api.h"
#include "Core/Event.h"
#include "GUI/Host/GUIWindowSession.h"
#include "GUI/Host/NativeWindowManager.h"
#include "RHI/Core/PresentFrame.h"

#include <cstdint>
#include <functional>
#include <memory>
#include <vector>

namespace ya
{

/// Extra native GUI windows that share the process IRender device.
///
/// Concrete `IGUIWindowCoordinator`: each session owns one INativeWindow +
/// one WidgetTree + one snapshot + one `IRenderSurfaceContext`. Focus,
/// hover, tooltip host, capture widget and DPI live on that tree. Pointer
/// capture/drag identity, OS clipboard, IME and cursor belong to the host
/// `GUIDragRouter`. It does
/// not call `IRender::create`. This manager holds the windows opened after the
/// startup one; the startup window is a session too (`GUIWindowHost`), and a
/// GUI app ticks every window's content before presenting any of them.
class YA_GUI_API GUIWindowManager final : public IGUIWindowCoordinator
{
    NativeWindowManager                           _nativeWindows;
    std::vector<std::unique_ptr<GUIWindowSession>> _sessions;
    GUIWindowId                                   _focusedId     = 0;
    GUIWindowId                                   _deferCloseA   = 0;
    GUIWindowId                                   _deferCloseB   = 0;
    bool                                          _bInitialized  = false;
    ScreenDrawPipelines*                          _screenPipelines = nullptr;

  public:
    GUIWindowManager() = default;
    ~GUIWindowManager() override;

    GUIWindowManager(const GUIWindowManager&)            = delete;
    GUIWindowManager& operator=(const GUIWindowManager&) = delete;

    [[nodiscard]] bool init();
    void               shutdown();

    /// Screen PSO cache shared by every session this manager opens. Set
    /// before `createSession` whenever those windows present.
    void setScreenDrawPipelines(ScreenDrawPipelines* pipelines) { _screenPipelines = pipelines; }

    [[nodiscard]] GUIWindowId createSession(const FGUIWindowHostConfig& config,
                                            IGUIAppDelegate&            delegate,
                                            IRender*                    render = nullptr) override;
    /// Alias for createSession (MW-101 call sites).
    [[nodiscard]] GUIWindowId create(const FGUIWindowHostConfig& config,
                                     IGUIAppDelegate&            delegate,
                                     IRender*                    render = nullptr)
    {
        return createSession(config, delegate, render);
    }
    void requestClose(GUIWindowId id) override;
    bool destroySession(GUIWindowId id) override;
    bool destroy(GUIWindowId id) { return destroySession(id); }

    [[nodiscard]] IGUIWindowSession*       findSession(GUIWindowId id) override;
    [[nodiscard]] const IGUIWindowSession* findSession(GUIWindowId id) const override;

    [[nodiscard]] WidgetTree*            findTree(GUIWindowId id) const;
    [[nodiscard]] INativeWindow*         findNative(GUIWindowId id) const;
    [[nodiscard]] const UIFrameSnapshot* findSnapshot(GUIWindowId id) const;
    [[nodiscard]] GUIWindowId            focusedWindowId() const { return _focusedId; }
    [[nodiscard]] size_t                 extraWindowCount() const { return _sessions.size(); }
    [[nodiscard]] bool                   isHostOverlay(GUIWindowId id) const override;

    /// Detach a session's WidgetTree without destroying the native window.
    /// The session tree pointer becomes null until `adoptTree`.
    [[nodiscard]] std::unique_ptr<WidgetTree> takeTree(GUIWindowId id);
    /// Replace a session's WidgetTree. Destroys the previous tree.
    bool adoptTree(GUIWindowId id, std::unique_ptr<WidgetTree> tree);

    /// `id == 0` clears extra focus so untagged keyboard events return to the
    /// primary window instead of the last extra session.
    void setFocusedWindow(GUIWindowId id);
    /// Route a Core event to the owning extra window. Returns false if this
    /// manager does not own the event's window id (caller should try primary).
    bool dispatchEvent(const Event& event);
    void tickAll(float dt);
    /// Tick extra trees and rebuild snapshots without flushing close-requested
    /// sessions. GameEditor redocks extras before destroy; GUIApp uses tickAll.
    void tickTrees(float dt);
    /// Record each extra's latest snapshot into `submission`. No-op when a
    /// session has no surface (tests without a shared device). Does not submit.
    void recordAll(FFrameSubmission& submission);
    /// Record every extra window and submit that set once. A caller that
    /// already has a frame submission uses `recordAll` instead, so these
    /// windows join that submission rather than starting another.
    void renderAll();

    /// Destroy any requestClose'd sessions except windows listed as deferred
    /// (cross-window drag keep-alive). Safe at the start of a kernel tick.
    void flushPendingCloses();
    void setDeferredCloseWindows(GUIWindowId a, GUIWindowId b);

    [[nodiscard]] GUIWindowId findDraggingWindowId() const;
    [[nodiscard]] GUIWindowId findCapturingWindowId() const;
    [[nodiscard]] GUIWindowId findModalWindowId() const;
    void forEachWindow(const std::function<void(GUIWindowId, WidgetTree*, INativeWindow*)>& fn) const;
    /// Visit this manager's sessions as windows, for a caller that owns several
    /// registries and treats them as one set.
    void forEachSession(const std::function<void(IGUIWindowSession&)>& fn) const;

  private:
    GUIWindowSession*       findOwnedSession(GUIWindowId id);
    const GUIWindowSession* findOwnedSession(GUIWindowId id) const;
    void                    destroyOwnedSession(GUIWindowSession& session);
    void                    dispatchToSession(GUIWindowSession& session, const Event& event);
};

} // namespace ya
