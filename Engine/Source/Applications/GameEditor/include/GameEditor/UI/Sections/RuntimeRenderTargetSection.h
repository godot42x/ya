#pragma once

#include "GUI/Widgets/CompoundWidget.h"

#include <memory>
#include <string>

namespace ya
{

struct App;
struct UIText;

/// Lists the render targets the renderer currently owns. The catalog is a
/// renderer fact, asked for through the app's render services.
class RuntimeRenderTargetSection final : public UICompoundWidget
{
  public:
    explicit RuntimeRenderTargetSection(std::string name = "RuntimeRenderTargets");
    void sync(const App* app);

  protected:
    void construct() override;

  private:
    std::shared_ptr<UIText> _summary;
    std::shared_ptr<UIText> _details;
};

} // namespace ya
