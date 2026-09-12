#pragma once

#include "GameEditor/UI/EditorDocumentSession.h"
#include "GameEditor/UI/EditorNestedDockHost.h"

namespace ya
{

enum class EEditorDocumentToolRole : uint8_t
{
    Preview,
    Parameters,
    Hierarchy,
    Inspector,
};

class EditorDocumentEditorTab : public EditorNestedDockHost
{
  public:
    EditorDocumentEditorTab(EditorRootId rootId, FEditorTabSpawnContext& ctx);
};

class EditorDocumentToolTab : public UICompoundWidget
{
  public:
    EditorDocumentToolTab(EEditorDocumentKind kind,
                          EEditorDocumentToolRole role,
                          FEditorTabSpawnContext& ctx);

    void onAttached() override;
    void tick(float deltaSeconds) override;

  protected:
    void construct() override;

  private:
    EEditorDocumentKind     _kind = EEditorDocumentKind::Material;
    EEditorDocumentToolRole _role = EEditorDocumentToolRole::Preview;
    EditorRootSession*      _ownerRoot = nullptr;
    EditorDocumentRegistry* _documents = nullptr;
    std::string             _documentKey;
    std::shared_ptr<struct UIText> _body;

    void refresh();
    [[nodiscard]] EditorDocumentSession* session() const;
};

} // namespace ya
