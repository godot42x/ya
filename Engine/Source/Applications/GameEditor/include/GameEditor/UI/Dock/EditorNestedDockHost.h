#pragma once

#include "GUI/Widgets/CompoundWidget.h"
#include "GameEditor/UI/Dock/EditorDockWorkspace.h"

#include <memory>

namespace ya
{

struct FDockContext;
struct FEditorTabSpawnContext;

/// WindowRootEditor body that hosts an editor-owned nested FDockContext.
/// Level Editor keeps Surface-owned nested dock; UI/Material/Script tabs
/// own theirs so Surface does not become a dock manager.
class EditorNestedDockHost : public UICompoundWidget
{
  protected:
    EditorRootId                  _rootId = kInvalidEditorRootId;
    std::shared_ptr<FDockContext> _nestedDock;
    EditorDockWorkspace           _nested;
    /// Set once the initial layout is in. Importing a layout fires the dock
    /// listeners, so subscribing before that would write the shipped default
    /// back out as if the user had arranged it -- and the default could then
    /// never improve.
    bool _bPersistsArrangement = false;

  public:
    EditorNestedDockHost(const char* name, EditorRootId rootId, FEditorTabSpawnContext& ctx);
    ~EditorNestedDockHost() override;

    [[nodiscard]] EditorRootId rootId() const { return _rootId; }
    [[nodiscard]] FDockContext* nestedDock() const { return _nestedDock.get(); }
    [[nodiscard]] EditorDockWorkspace& nestedWorkspace() { return _nested; }
    [[nodiscard]] const EditorDockWorkspace& nestedWorkspace() const { return _nested; }

    /// Apply this root's layout: the arrangement this machine last had, else
    /// the shipped document. Called once, from the constructing page tab.
    void applyNestedFactoryLayout(const nlohmann::json& layout);

    /// Write this dock's arrangement to the overrides root. Wired as a dock
    /// listener, so a split drag or tab move survives the page being closed.
    void rememberLayout();

  protected:
    void construct() override;
};

} // namespace ya
