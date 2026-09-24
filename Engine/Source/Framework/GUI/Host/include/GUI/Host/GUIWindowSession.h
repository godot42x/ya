#pragma once

#include "GUI/Host/GUIAppDelegate.h"
#include "GUI/Host/GUIWindowChrome.h"
#include "GUI/Host/GUIAppHost.h"
#include "GUI/Host/GUIWindowPresent.h"
#include "GUI/Widgets/UIFrameSnapshot.h"
#include "GUI/Widgets/WidgetTree.h"
#include "RHI/Core/RenderSurfaceContext.h"
#include "RHI/NativeWindow.h"
#include "RHI/Render.h"
#include "Render2D/Render2D.h"

#include <memory>

namespace ya
{

/// One native GUI window's live session. Not an editor/document object.
///
/// Owns the NativeWindow handle, WidgetTree, snapshot, input/focus state on
/// that tree, and optional `IRenderSurfaceContext` presentation set. Shared
/// device stays on the process `IRender`; this object never calls
/// `IRender::create`.
class YA_GUI_API IGUIWindowSession
{
  public:
    virtual ~IGUIWindowSession() = default;

    [[nodiscard]] virtual GUIWindowId              id() const             = 0;
    [[nodiscard]] virtual INativeWindow*           nativeWindow() const   = 0;
    [[nodiscard]] virtual WidgetTree*              tree() const           = 0;
    [[nodiscard]] virtual const UIFrameSnapshot*   snapshot() const       = 0;
    [[nodiscard]] virtual IRenderSurfaceContext*   surfaceContext() const = 0;
    [[nodiscard]] virtual bool                     isMinimized() const    = 0;
    [[nodiscard]] virtual bool                     closeRequested() const = 0;
    [[nodiscard]] virtual const FWindowChromeState& chrome() const        = 0;
    /// Click-through drag preview window. Not an editor/session extra.
    [[nodiscard]] virtual bool isHostOverlay() const { return false; }
};

/// Extra-window owner used by `GUIWindowManager`. Primary `GUIWindowHost`
/// stays a separate owner until a later unify; extras are sessions.
class YA_GUI_API GUIWindowSession final : public IGUIWindowSession
{
  public:
    GUIWindowId                          windowId = 0;
    FGUIWindowHostConfig                 config;
    IGUIAppDelegate*                     delegate = nullptr;
    INativeWindow*                       native   = nullptr;
    std::unique_ptr<WidgetTree>          ownedTree;
    UIFrameSnapshot                      ownedSnapshot;
    float                                lastMouseX = -1.0f;
    float                                lastMouseY = -1.0f;
    bool                                 bCloseRequested           = false;
    bool                                 bMinimized                = false;
    bool                                 bSwapchainRecreatePending = false;
    Render2DPassSlot                     presentPassSlot   = kInvalidRender2DPassSlot;
    Render2DPassSlot                     offscreenPassSlot = kInvalidRender2DPassSlot;
    /// The device's surface for this window, named by `surfaceId`. The device
    /// owns it (see `IRender::createSurfaceContext`), so a session holds the id
    /// and the resolved pointer, never a `unique_ptr` that would make surface
    /// lifetime a session detail.
    SurfaceId                            surfaceId{};
    IRenderSurfaceContext*                present = nullptr;
    FGUISurfacePresentResources          presentResources;
    FWindowChromeState                   chromeState;

    [[nodiscard]] GUIWindowId            id() const override { return windowId; }
    [[nodiscard]] INativeWindow*         nativeWindow() const override { return native; }
    [[nodiscard]] WidgetTree*            tree() const override { return ownedTree.get(); }
    [[nodiscard]] const UIFrameSnapshot* snapshot() const override { return &ownedSnapshot; }
    [[nodiscard]] IRenderSurfaceContext* surfaceContext() const override { return present; }
    [[nodiscard]] bool isMinimized() const override { return bMinimized; }
    [[nodiscard]] bool closeRequested() const override { return bCloseRequested; }
    [[nodiscard]] const FWindowChromeState& chrome() const override { return chromeState; }
    [[nodiscard]] bool isHostOverlay() const override { return config.bDragOverlay; }
};

/// Framework window lifecycle. GameEditor must not implement this.
/// This interface never names dock types. Native dock placement is a Host
/// adapter over `createSession` (`GUIDockNativePlacement.h`).
class YA_GUI_API IGUIWindowCoordinator
{
  public:
    virtual ~IGUIWindowCoordinator() = default;

    /// Create an extra native window + WidgetTree on the shared device.
    /// `render` may be null in tests (no surface). Does not call `IRender::create`.
    [[nodiscard]] virtual GUIWindowId createSession(const FGUIWindowHostConfig& config,
                                                    IGUIAppDelegate&            delegate,
                                                    IRender*                    render = nullptr) = 0;
    virtual void                      requestClose(GUIWindowId id)                                = 0;
    virtual bool                      destroySession(GUIWindowId id)                              = 0;

    [[nodiscard]] virtual IGUIWindowSession*       findSession(GUIWindowId id)       = 0;
    [[nodiscard]] virtual const IGUIWindowSession* findSession(GUIWindowId id) const = 0;

    [[nodiscard]] virtual bool isHostOverlay(GUIWindowId id) const
    {
        (void)id;
        return false;
    }
};

} // namespace ya
