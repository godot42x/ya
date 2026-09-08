#pragma once

#include "Core/Api.h"
#include "Core/Event.h"
#include "GUI/Host/GUIAppDelegate.h"
#include "GUI/Host/GUIWindowHost.h"
#include "GUI/Host/NativeWindowManager.h"
#include "GUI/Widgets/UIFrameSnapshot.h"
#include "GUI/Widgets/WidgetTree.h"
#include "RHI/NativeWindow.h"

#include <cstdint>
#include <memory>
#include <vector>

namespace ya
{

/// Extra native GUI windows that share the process IRender device.
///
/// Each slot owns one INativeWindow + one WidgetTree + one snapshot. Pointer,
/// focus, capture, tooltip, clipboard and DPI live on that tree. It does not
/// call IRender::create and does not present (C2). The primary window stays
/// on GUIWindowHost; this manager only holds extras. One AppKernel tick
/// calls tickAll after the primary host tick.
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
        bool                     bMinimized      = false;
    };

    std::vector<std::unique_ptr<FSlot>> _slots;
    GUIWindowId                         _focusedId = 0;
    bool                                _bInitialized = false;

  public:
    GUIWindowManager() = default;
    ~GUIWindowManager();

    GUIWindowManager(const GUIWindowManager&)            = delete;
    GUIWindowManager& operator=(const GUIWindowManager&) = delete;

    [[nodiscard]] bool init();
    void               shutdown();

    /// Create an extra OS window + WidgetTree. Does not create a GPU device.
    [[nodiscard]] GUIWindowId create(const FGUIWindowHostConfig& config, IGUIAppDelegate& delegate);
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
    /// C2 will present extras. MW-102 is tree/input/snapshot isolation only.
    void renderAll() {}

    /// Destroy any requestClose'd slots. Safe at the start of a kernel tick.
    void flushPendingCloses();

  private:
    FSlot*       findSlot(GUIWindowId id);
    const FSlot* findSlot(GUIWindowId id) const;
    void         destroySlot(FSlot& slot);
    void         dispatchToSlot(FSlot& slot, const Event& event);
};

[[nodiscard]] uint32_t guiEventWindowId(const Event& event);

} // namespace ya
