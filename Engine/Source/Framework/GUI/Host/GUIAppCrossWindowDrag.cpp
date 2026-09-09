#include "GUI/Host/GUIAppHost.h"
#include "GUI/Host/GUIWindowManager.h"

#include "Core/Event.h"
#include "Core/KeyCode.h"
#include "GUI/Widgets/DragDropOperation.h"
#include "GUI/Widgets/WidgetTree.h"

namespace ya
{

namespace
{

[[nodiscard]] bool isFarPointer(glm::vec2 point)
{
    return point.x < -10000.0f || point.y < -10000.0f;
}

} // namespace

bool GUIApp::isCrossWindowDragActive() const
{
    return _crossWindowDrag.sourceTree != nullptr && _crossWindowDrag.sourceTree->isDragging();
}

GUIWindowId GUIApp::crossWindowDragSourceId() const
{
    return isCrossWindowDragActive() ? _crossWindowDrag.sourceWindowId : 0;
}

GUIWindowId GUIApp::crossWindowDragHoverId() const
{
    return isCrossWindowDragActive() ? _crossWindowDrag.hoverWindowId : 0;
}

uint32_t GUIApp::crossWindowDragEnterCount() const
{
    return _crossWindowDrag.boundaryEnterCount;
}

uint32_t GUIApp::crossWindowDragLeaveCount() const
{
    return _crossWindowDrag.boundaryLeaveCount;
}

void GUIApp::runAfterDrag(std::function<void()> fn)
{
    if (!fn) {
        return;
    }
    if (!isCrossWindowDragActive()) {
        fn();
        return;
    }
    _afterDrag.push_back(std::move(fn));
}

void GUIApp::runQueuedAfterDrag()
{
    auto callbacks = std::move(_afterDrag);
    _afterDrag.clear();
    for (auto& fn : callbacks) {
        if (fn) {
            fn();
        }
    }
}

void GUIApp::adoptDragSource()
{
    if (isCrossWindowDragActive()) {
        return;
    }

    WidgetTree* found   = nullptr;
    GUIWindowId foundId = 0;
    if (_primaryWindow.isInitialized()) {
        WidgetTree& tree = _primaryWindow.getTree();
        if (tree.isDragging()) {
            found   = &tree;
            foundId = _primaryWindow.getWindowID();
        }
    }
    if (!found) {
        foundId = _extraWindows->findDraggingWindowId();
        found   = foundId != 0 ? _extraWindows->findTree(foundId) : nullptr;
    }
    if (!found) {
        return;
    }

    _crossWindowDrag.sourceWindowId = foundId;
    _crossWindowDrag.sourceTree     = found;
    _crossWindowDrag.hoverWindowId  = foundId;
    _crossWindowDrag.hoverTree      = found;
}

void GUIApp::syncCrossWindowDrag()
{
    if (_crossWindowDrag.sourceTree && !_crossWindowDrag.sourceTree->isDragging()) {
        if (_crossWindowDrag.hoverTree &&
            _crossWindowDrag.hoverTree != _crossWindowDrag.sourceTree) {
            _crossWindowDrag.hoverTree->clearExternalDropHover();
        }
        _crossWindowDrag = {};
        runQueuedAfterDrag();
        return;
    }
    adoptDragSource();
}

void GUIApp::finishCrossWindowDrag(EDragFinishResult result)
{
    if (_crossWindowDrag.hoverTree &&
        _crossWindowDrag.hoverTree != _crossWindowDrag.sourceTree) {
        _crossWindowDrag.hoverTree->clearExternalDropHover();
    }
    if (_crossWindowDrag.sourceTree) {
        _crossWindowDrag.sourceTree->finishDrag(result);
    }
    _crossWindowDrag = {};
    runQueuedAfterDrag();
}

void GUIApp::cancelCrossWindowDrag()
{
    if (_crossWindowDrag.hoverTree &&
        _crossWindowDrag.hoverTree != _crossWindowDrag.sourceTree) {
        _crossWindowDrag.hoverTree->clearExternalDropHover();
    }
    if (_crossWindowDrag.sourceTree) {
        _crossWindowDrag.sourceTree->cancelDrag();
    }
    _crossWindowDrag = {};
    runQueuedAfterDrag();
}

void GUIApp::applyDeferredCloses()
{
    if (isCrossWindowDragActive()) {
        _extraWindows->setDeferredCloseWindows(_crossWindowDrag.sourceWindowId,
                                               _crossWindowDrag.hoverWindowId);
    }
    else {
        _extraWindows->setDeferredCloseWindows(0, 0);
    }
    _extraWindows->flushPendingCloses();
}

void GUIApp::rememberPointer(const Event& event)
{
    if (event.getEventType() != EEvent::MouseMoved) {
        return;
    }
    const auto& move     = static_cast<const MouseMoveEvent&>(event);
    _lastPointer         = {move.getX(), move.getY()};
    _lastPointerWindow   = move.getWindowID();
}

glm::vec2 GUIApp::pointerForEvent(const Event& event) const
{
    if (event.getEventType() == EEvent::MouseMoved) {
        const auto& move = static_cast<const MouseMoveEvent&>(event);
        return {move.getX(), move.getY()};
    }
    return _lastPointer;
}

void GUIApp::setHoverWindow(GUIWindowId id, WidgetTree* tree, glm::vec2 point)
{
    if (_crossWindowDrag.hoverTree &&
        _crossWindowDrag.hoverTree != _crossWindowDrag.sourceTree &&
        _crossWindowDrag.hoverTree != tree) {
        _crossWindowDrag.hoverTree->clearExternalDropHover();
    }

    const bool bForeign = tree != nullptr && tree != _crossWindowDrag.sourceTree;
    if (bForeign && id != _crossWindowDrag.hoverWindowId) {
        ++_crossWindowDrag.boundaryEnterCount;
    }

    _crossWindowDrag.hoverWindowId = id;
    _crossWindowDrag.hoverTree     = tree;

    const UIDragDropOperation* operation =
        _crossWindowDrag.sourceTree ? _crossWindowDrag.sourceTree->getDragOperation() : nullptr;
    if (bForeign && operation) {
        tree->setExternalDropHover(*operation, point);
    }
}

bool GUIApp::routeCrossWindowDrag(const Event& event)
{
    if (!isCrossWindowDragActive()) {
        return false;
    }

    const EEvent::T type     = event.getEventType();
    const GUIWindowId eventId = guiEventWindowId(event);

    if (type == EEvent::KeyPressed) {
        const auto& key = static_cast<const KeyPressedEvent&>(event);
        if (key.getKeyCode() == EKey::Escape && !key.isRepeat()) {
            cancelCrossWindowDrag();
            return true;
        }
        return false;
    }

    auto noteLeave = [this](GUIWindowId leaveId) {
        ++_crossWindowDrag.boundaryLeaveCount;
        if (_crossWindowDrag.hoverWindowId != leaveId) {
            return;
        }
        if (_crossWindowDrag.hoverTree &&
            _crossWindowDrag.hoverTree != _crossWindowDrag.sourceTree) {
            _crossWindowDrag.hoverTree->clearExternalDropHover();
        }
        _crossWindowDrag.hoverWindowId = 0;
        _crossWindowDrag.hoverTree     = nullptr;
    };

    if (type == EEvent::WindowMouseLeave) {
        noteLeave(eventId);
        return true;
    }

    if (type == EEvent::MouseMoved) {
        rememberPointer(event);
        const glm::vec2 point = pointerForEvent(event);
        WidgetTree*     tree  = eventId != 0 ? findTree(eventId) : nullptr;
        if (isFarPointer(point)) {
            noteLeave(eventId);
            return tree != _crossWindowDrag.sourceTree;
        }
        if (!tree || tree == _crossWindowDrag.sourceTree) {
            return false;
        }
        setHoverWindow(eventId, tree, point);
        return true;
    }

    if (type == EEvent::MouseButtonReleased) {
        WidgetTree* tree = eventId != 0 ? findTree(eventId) : nullptr;
        if (!tree || tree == _crossWindowDrag.sourceTree) {
            return false;
        }
        const glm::vec2 point = pointerForEvent(event);
        const UIDragDropOperation* operation =
            _crossWindowDrag.sourceTree->getDragOperation();
        const bool bDropped = operation && tree->dropExternal(*operation, point);
        finishCrossWindowDrag(bDropped ? EDragFinishResult::Dropped : EDragFinishResult::NoTarget);
        return true;
    }

    if (type == EEvent::MouseButtonPressed) {
        WidgetTree* tree = eventId != 0 ? findTree(eventId) : nullptr;
        if (!tree || tree == _crossWindowDrag.sourceTree) {
            return false;
        }
        cancelCrossWindowDrag();
        return true;
    }

    return false;
}

} // namespace ya
