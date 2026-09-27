#pragma once

#include "Core/Common/Types.h"
#include "Core/Event.h"
#include "GUI/Widgets/UIFrameSnapshot.h"
#include "GameEditor/UI/Shell/EditorDocumentSession.h"
#include "GameEditor/UI/Shell/EditorRootSession.h"
#include "GameEditor/UI/Shell/EditorSurface.h"
#include "GameEditor/UI/Shell/EditorSurfaceContext.h"

#include <string_view>

namespace ya
{

struct App;
struct WidgetTree;
enum class EWidgetRouteResult : uint8_t;

/// One native editor window. The session owns the chrome Surface and
/// references the active root editor; it does not absorb selection/undo/actions
/// (those live on EditorRootSession) or become a window manager. Extra sessions
/// must not share the default window's WidgetTree.
///
/// `tick` is the Module-side chrome entry for one window. The default window
/// is ticked from `EditorModule::onPresentation`; extra windows tick from
/// `EditorModule::recordExtraSurfaces` via `GUIWindowManager`.
struct EditorWindowSession
{
private:
    EditorWindowId      _windowId = kDefaultEditorWindowId;
    EditorRootSession   _levelRoot{kLevelEditorRootId};
    EditorRootSession   _uiRoot{kUIEditorRootId};
    EditorRootSession   _materialRoot{kMaterialEditorRootId};
    EditorRootSession   _scriptRoot{kScriptEditorRootId};
    EditorSurface       _surface;
    EditorWindowMetrics _metrics{};
    EditorDocumentRegistry* _documents = nullptr;
    WidgetTree*         _hostTree = nullptr;
    uint32_t            _hostGuiWindowId = 0;

public:
    explicit EditorWindowSession(EditorWindowId windowId = kDefaultEditorWindowId)
        : _windowId(windowId)
    {
    }

    [[nodiscard]] EditorWindowId windowId() const { return _windowId; }
    [[nodiscard]] EditorRootSession& activeRoot() { return _levelRoot; }
    [[nodiscard]] const EditorRootSession& activeRoot() const { return _levelRoot; }
    [[nodiscard]] EditorRootSession& root(EditorRootId id)
    {
        switch (id) {
        case kUIEditorRootId: {
            return _uiRoot;
        }
        case kMaterialEditorRootId: {
            return _materialRoot;
        }
        case kScriptEditorRootId: {
            return _scriptRoot;
        }
        default: {
            return _levelRoot;
        }
        }
    }
    [[nodiscard]] const EditorRootSession& root(EditorRootId id) const
    {
        switch (id) {
        case kUIEditorRootId: {
            return _uiRoot;
        }
        case kMaterialEditorRootId: {
            return _materialRoot;
        }
        case kScriptEditorRootId: {
            return _scriptRoot;
        }
        default: {
            return _levelRoot;
        }
        }
    }
    [[nodiscard]] FEditorRootSessions roots()
    {
        return {.level = &_levelRoot, .ui = &_uiRoot, .material = &_materialRoot, .script = &_scriptRoot};
    }
    [[nodiscard]] EditorSurface& surface() { return _surface; }
    [[nodiscard]] const EditorSurface& surface() const { return _surface; }
    [[nodiscard]] const EditorWindowMetrics& metrics() const { return _metrics; }

    void bind(EditorLayer& layer,
              EditorTabSpawnerRegistry* spawners = nullptr,
              EditorDocumentRegistry* documents = nullptr);

    /// Bind (or retarget) the Level scene document. Same path is a singleton:
    /// two windows share undo. Previous unused scene sessions are discarded.
    void bindSceneDocument(EditorDocumentRegistry& documents, std::string_view path);

    void tick(const FEditorSurfaceContext& context, float dt);

    [[nodiscard]] EWidgetRouteResult dispatchEvent(const Event& event, const glm::vec2& windowPoint)
    {
        return _surface.dispatchEvent(event, windowPoint);
    }
    [[nodiscard]] const UIFrameSnapshot& snapshot() const { return _surface.snapshot(); }
    /// Extra native windows host chrome on the coordinator session tree.
    /// Default product window keeps the Surface-owned tree.
    void adoptHostTree(WidgetTree* tree, uint32_t guiWindowId = 0)
    {
        _hostTree = tree;
        _hostGuiWindowId = guiWindowId;
    }
    [[nodiscard]] WidgetTree* tree() const { return _hostTree ? _hostTree : _surface.tree(); }
    [[nodiscard]] uint32_t hostGuiWindowId() const { return _hostGuiWindowId; }
    [[nodiscard]] bool wantsTextInput() const { return _surface.wantsTextInput(); }
    [[nodiscard]] bool isViewportHovered() const { return _surface.isViewportHovered(); }
    [[nodiscard]] bool isViewportFocused() const { return _surface.isViewportFocused(); }
    [[nodiscard]] bool isViewportOverlayActive() const { return _surface.isViewportOverlayActive(); }
    [[nodiscard]] bool isPointInViewport(const glm::vec2& windowPoint) const
    {
        return _surface.isPointInViewport(windowPoint);
    }

    void shutdown()
    {
        _levelRoot.bindDocument(nullptr);
        _uiRoot.bindDocument(nullptr);
        _materialRoot.bindDocument(nullptr);
        _scriptRoot.bindDocument(nullptr);
        _hostTree = nullptr;
        _hostGuiWindowId = 0;
        _surface.shutdown();
    }
};

} // namespace ya
