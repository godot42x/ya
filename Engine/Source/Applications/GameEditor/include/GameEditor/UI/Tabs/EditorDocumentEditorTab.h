#pragma once

#include "GameEditor/UI/Shell/EditorDocumentSession.h"
#include "GameEditor/UI/Dock/EditorNestedDockHost.h"

#include <memory>
#include <string>

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

    /// Minimal source preview (script documents): the file's text, read-only
    /// and scrollable. Null for roles/kinds that have no source to show.
    std::shared_ptr<struct UIText>       _sourceText;
    std::shared_ptr<struct UIScrollViewport> _sourceScroll;
    /// Source caching: the text only changes when another writer saves the
    /// file, so the read is gated on the document's clean/dirty transition.
    std::string _sourceKey;
    bool        _sourceDirty = false;
    bool        _bHasSource  = false;

    void refresh();
    void refreshSource(EditorDocumentSession& open);
    [[nodiscard]] EditorDocumentSession* session() const;
};

} // namespace ya
