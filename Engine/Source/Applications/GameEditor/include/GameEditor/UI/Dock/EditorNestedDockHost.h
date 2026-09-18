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

  public:
    EditorNestedDockHost(const char* name, EditorRootId rootId, FEditorTabSpawnContext& ctx);

    [[nodiscard]] EditorRootId rootId() const { return _rootId; }
    [[nodiscard]] FDockContext* nestedDock() const { return _nestedDock.get(); }
    [[nodiscard]] EditorDockWorkspace& nestedWorkspace() { return _nested; }
    [[nodiscard]] const EditorDockWorkspace& nestedWorkspace() const { return _nested; }

    void applyNestedFactoryLayout(const nlohmann::json& layout);

  protected:
    void construct() override;
};

} // namespace ya
