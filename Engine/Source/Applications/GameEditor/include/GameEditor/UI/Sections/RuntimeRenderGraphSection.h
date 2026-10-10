#pragma once

#include "GUI/Widgets/CompoundWidget.h"
#include "GameEditor/UI/Shell/EditorStatReadouts.h"

#include <memory>

namespace ya
{
struct App;
struct UIText;

class RuntimeRenderGraphSection final : public UICompoundWidget
{
  public:
    explicit RuntimeRenderGraphSection(std::string name = "RuntimeRenderGraph");
    void sync(const App* app);

  protected:
    void construct() override;

  private:
    FStatReadout _pipeline;
    FStatReadout _passes;
    FStatReadout _dependencies;
    std::shared_ptr<UIText> _status;
};
} // namespace ya
