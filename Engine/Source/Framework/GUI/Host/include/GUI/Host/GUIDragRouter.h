#pragma once

#include "Core/Api.h"
#include "Core/Delegate.h"
#include "Core/Event.h"
#include "Core/Input/Cursor.h"
#include "GUI/Widgets/WidgetTree.h"

#include <cstdint>
#include <functional>
#include <glm/glm.hpp>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace ya
{

class GUIWindowManager;
struct INativeWindow;
struct IRender;
struct IGUIAppDelegate;

/// Unique pointer-universe session for one AppKernel (one mouse, one OS
/// clipboard/IME/cursor identity).
///
/// WidgetTree still holds source-local payload, ghost, capture widget,
/// hover, tooltip, and modal overlay. This router is the only object allowed
/// to say which window currently owns the pointer (drag source / capture),
/// which window is the hover/drop target, and which window may receive IME.
/// GUIApp and GameEditor both bind their trees here.
///
/// Not a process singleton: gtest and headless hosts each own one router.
class YA_GUI_API GUIDragRouter
{
    uint32_t          _primaryId   = 0;
    WidgetTree*       _primaryTree = nullptr;
    INativeWindow*    _primaryNative = nullptr;
    GUIWindowManager* _extras      = nullptr;
    IRender*          _render      = nullptr;
    std::unordered_map<uint32_t, WidgetTree*> _windows;

    uint32_t    _sourceWindowId = 0;
    uint32_t    _hoverWindowId  = 0;
    WidgetTree* _sourceTree     = nullptr;
    WidgetTree* _hoverTree      = nullptr;
    uint32_t    _captureWindowId = 0;
    WidgetTree* _captureTree     = nullptr;
    uint32_t    _boundaryEnterCount = 0;
    uint32_t    _boundaryLeaveCount = 0;
    glm::vec2   _lastPointer{};
    uint32_t    _lastPointerWindow = 0;
    std::vector<std::function<void()>> _afterDrag;

    uint32_t                       _overlayId = 0;
    bool                           _bWantsDesktopOverlay = false;
    bool                           _bMouseCaptured = false;
    std::unique_ptr<IGUIAppDelegate> _overlayDelegate;
    std::string                    _overlayLabel;
    glm::vec2                      _lastScreen{};

    /// Bound trees can be destroyed by their owners (the editor rebuilds its
    /// chrome into a fresh WidgetTree). Each bound tree's onDestroyed drops
    /// every cached pointer to it, so no consumer dereferences a dead tree in
    /// the window between the rebuild and the next bindPrimary.
    std::unordered_map<WidgetTree*, DelegateHandle> _treeDeathWatches;

public:
    void bindPrimary(uint32_t id, WidgetTree* tree, INativeWindow* native = nullptr);
    void bindExtras(GUIWindowManager* extras);
    void bindRender(IRender* render);
    /// Extra lookup for tests / hosts that are not GUIWindowManager sessions.
    void bindWindow(uint32_t id, WidgetTree* tree);
    void unbind();
    ~GUIDragRouter();

    [[nodiscard]] WidgetTree*    findTree(uint32_t id) const;
    [[nodiscard]] INativeWindow* findNative(uint32_t id) const;

    [[nodiscard]] bool     isActive() const;
    [[nodiscard]] bool     isCaptureActive() const;
    [[nodiscard]] uint32_t sourceId() const;
    [[nodiscard]] uint32_t hoverId() const;
    [[nodiscard]] uint32_t captureId() const;
    [[nodiscard]] uint32_t modalWindowId() const;
    [[nodiscard]] uint32_t textInputWindowId() const;
    [[nodiscard]] ECursorType cursor() const;
    [[nodiscard]] uint32_t    enterCount() const { return _boundaryEnterCount; }
    [[nodiscard]] uint32_t    leaveCount() const { return _boundaryLeaveCount; }
    [[nodiscard]] bool        wantsDesktopOverlay() const { return _bWantsDesktopOverlay; }
    [[nodiscard]] uint32_t    overlayWindowId() const { return _overlayId; }

    void adoptSource();
    void adoptCapture();
    void sync();
    void syncTextInput();
    [[nodiscard]] bool route(const Event& event);
    void finish(EDragFinishResult result);
    void cancel();
    void applyDeferredCloses();
    void runAfterDrag(std::function<void()> fn);

private:
    void runQueuedAfterDrag();
    void watchTree(WidgetTree* tree);
    void forgetTree(WidgetTree* tree);
    void setHoverWindow(uint32_t id, WidgetTree* tree, glm::vec2 point);
    void rememberPointer(const Event& event);
    [[nodiscard]] glm::vec2 pointerForEvent(const Event& event) const;
    [[nodiscard]] glm::vec2 toWindowLocal(uint32_t fromId, glm::vec2 fromLocal, uint32_t toId) const;
    [[nodiscard]] uint32_t findDraggingWindowId() const;
    [[nodiscard]] uint32_t findCapturingWindowId() const;
    [[nodiscard]] uint32_t findModalWindowId() const;
    [[nodiscard]] bool routeDrag(const Event& event);
    [[nodiscard]] bool routeCapture(const Event& event);
    [[nodiscard]] bool routeModal(const Event& event);
    void forEachBoundWindow(const std::function<void(uint32_t, WidgetTree*, INativeWindow*)>& fn) const;
    [[nodiscard]] bool isOverlayId(uint32_t id) const;
    [[nodiscard]] glm::vec2 toScreen(uint32_t fromId, glm::vec2 fromLocal) const;
    [[nodiscard]] glm::vec2 screenToLocal(uint32_t toId, glm::vec2 screen) const;
    [[nodiscard]] glm::vec2 dragScreenPoint(uint32_t eventId, glm::vec2 local) const;
    [[nodiscard]] uint32_t hitTestScreen(glm::vec2 screen) const;
    [[nodiscard]] uint32_t hitTestWindow(uint32_t eventId, glm::vec2 local) const;
    void setMouseCapture(bool capture);
    void destroyOverlay();
    void syncDesktopOverlay(glm::vec2 screen);
    void clearDesktopOverlayState();
    void syncHiddenSourceWindow(uint32_t hit);
    void restoreHiddenSourceWindow();
};

} // namespace ya
