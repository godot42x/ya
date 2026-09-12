#pragma once

#include "GameEditor/UI/EditorNestedDockHost.h"

namespace ya
{

struct EditorLayer;

/// UI WindowRootEditor host. Palette / tree / inspector / preview chrome
/// live in nested owned tools; document/preview WidgetTree stay on
/// UIDesignerPanel (not the Level Editor tree).
class EditorUIDesignerTab : public EditorNestedDockHost
{
  public:
    explicit EditorUIDesignerTab(FEditorTabSpawnContext& ctx);

    void onAttached() override;
    void tick(float deltaSeconds) override;

  protected:
    void construct() override;

  private:
    EditorLayer* _layer = nullptr;
    std::shared_ptr<struct UIText> _statusText;
    std::shared_ptr<struct UIButton> _newButton;
    std::shared_ptr<struct UIButton> _saveButton;
    std::shared_ptr<struct UIButton> _closeButton;

    void refreshStatus();
};

} // namespace ya
