#pragma once

#include "Core/Api.h"
#include "GUI/Host/GUIWindowChrome.h"

#include <cstdint>

namespace ya
{

struct INativeWindow;
struct WidgetTree;
struct UIFrameSnapshot;
struct IRenderSurfaceContext;

/// Stable numeric id of one GUI OS window. It is the native window's id, so a
/// window is named the same way by the platform, the GUI host and the app.
using GUIWindowId = uint32_t;

/// One native GUI window's live session. Not an editor/document object.
///
/// Owns the NativeWindow handle, WidgetTree, snapshot, input/focus state on
/// that tree, and optional `IRenderSurfaceContext` presentation set. Shared
/// device stays on the process `IRender`; this object never calls
/// `IRender::create`.
///
/// Every window is one of these, including the one a GUI app started with: a
/// registry that holds sessions has no "first" entry with a different shape,
/// so lookup, event routing, tick and close all work on a window that was named
/// rather than on the shape it happened to be created in.
struct YA_GUI_API IGUIWindowSession
{
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

} // namespace ya
