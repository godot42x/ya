#pragma once

#include "GUI/Widgets/CompoundWidget.h"

#include <memory>

namespace ya
{

struct FDockContext;

/// Non-closable Level Editor window-root. Hosts the owner nested DockSpace;
/// it does not own selection/undo or become a window manager.
class EditorLevelEditorTab : public UICompoundWidget
{
  private:
    std::shared_ptr<FDockContext> _nestedDock;

  public:
    explicit EditorLevelEditorTab(std::shared_ptr<FDockContext> nestedDock);

  protected:
    void construct() override;
};

} // namespace ya
