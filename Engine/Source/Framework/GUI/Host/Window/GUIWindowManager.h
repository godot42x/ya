#pragma once

#include "Core/Api.h"
#include "Core/Event.h"
#include "GUI/Host/GUIAppDelegate.h"
#include "GUI/Host/GUIWindowHost.h"
#include "GUI/Host/NativeWindowManager.h"
#include "GUI/Widgets/UIFrameSnapshot.h"
#include "GUI/Widgets/WidgetTree.h"
#include "GUI/Host/GUIPresentationTarget.h"
#include "GUI/Host/GUIWindowPresent.h"
#include "RHI/NativeWindow.h"
#include "RHI/Render.h"
#include "Render2D/Render2D.h"

#include <cstdint>
#include <memory>
#include <vector>

namespace ya
{

/// Extra native GUI windows that share the process IRender device.
///
/// Each slot owns one INativeWindow + one WidgetTree + one snapshot + one
/// `IRenderSurfaceContext`. Pointer, focus, capture, tooltip, clipboard and
/// DPI live on that tree. It does not call `IRender::create`. The primary
/// window stays on GUIWindowHost; this manager only holds extras. One
/// AppKernel tick calls tickAll then renderAll after the primary host tick.
class YA_GUI_API GUIWindowManager
{
    NativeWindowManager _nativeWindows;

    struct FSlot
    {
        GUIWindowId              id = 0;
        FGUIWindowHostConfig     config;
        IGUIAppDelegate*         delegate = nullptr;
        INativeWindow*           native   = nullptr;
        std::unique_ptr<WidgetTree> tree;
        UIFrameSnapshot          snapshot;
        float                    lastMouseX = -1.0f;
        float                    lastMouseY = -1.0f;
        bool                     bCloseRequested = false;
        /// Skip this slot's present only; tickAll still updates UI.
        bool                     bMinimized      = false;
        bool                     bSwapchainRecreatePending = false;
        Render2DPassSlot         presentPassSlot   = kInvalidRender2DPassSlot;
        Render2DPassSlot         offscreenPassSlot = kInvalidRender2DPassSlot;
        std::unique_ptr<IRenderSurfaceContext> ownedPresent;
        FGUISurfacePresentResources            presentResources;
    };

    std::vector<std::unique_ptr<FSlot>> _slots;
    GUIWindowId                         _focusedId = 0;
    GUIWindowId                         _deferCloseA = 0;
    GUIWindowId                         _deferCloseB = 0;
    bool                                _bInitialized = false;

  public:
    GUIWindowManager() = default;
    ~GUIWindowManager();

    GUIWindowManager(const GUIWindowManager&)            = delete;
    GUIWindowManager& operator=(const GUIWindowManager&) = delete;

    [[nodiscard]] bool init();
    void               shutdown();

    /// Create an extra OS window + WidgetTree. When `render` is non-null,
    /// also creates a surface context on that shared device. Does not call
    /// `IRender::create`.
    [[nodiscard]] GUIWindowId create(const FGUIWindowHostConfig& config,
                                     IGUIAppDelegate&            delegate,
                                     IRender*                    render = nullptr);
    void                      requestClose(GUIWindowId id);
    /// Immediate destroy. Prefer requestClose so teardown happens at a tick boundary.
    bool                      destroy(GUIWindowId id);

    [[nodiscard]] WidgetTree*    findTree(GUIWindowId id) const;
    [[nodiscard]] INativeWindow* findNative(GUIWindowId id) const;
    [[nodiscard]] const UIFrameSnapshot* findSnapshot(GUIWindowId id) const;
    [[nodiscard]] GUIWindowId    focusedWindowId() const { return _focusedId; }
    [[nodiscard]] size_t         extraWindowCount() const { return _slots.size(); }

    void setFocusedWindow(GUIWindowId id);
    /// Route a Core event to the owning extra window. Returns false if this
    /// manager does not own the event's window id (caller should try primary).
    bool dispatchEvent(const Event& event);
    void tickAll(float dt);
    /// Present each extra's latest snapshot to its own swapchain. No-op when
    /// a slot has no surface (tests without a shared device).
    void renderAll();

    /// Destroy any requestClose'd slots except windows listed as deferred
    /// (cross-window drag keep-alive). Safe at the start of a kernel tick.
    void flushPendingCloses();
    void setDeferredCloseWindows(GUIWindowId a, GUIWindowId b);

    [[nodiscard]] GUIWindowId findDraggingWindowId() const;

  private:
    FSlot*       findSlot(GUIWindowId id);
    const FSlot* findSlot(GUIWindowId id) const;
    void         destroySlot(FSlot& slot);
    void         dispatchToSlot(FSlot& slot, const Event& event);
};

[[nodiscard]] uint32_t guiEventWindowId(const Event& event);

} // namespace ya
