#pragma once

#include "GUI/Widgets/CompoundWidget.h"

#include <memory>

namespace ya
{

struct App;
struct IRenderSurfaceContext;
class RuntimeRenderSettingsSection;

/// WindowTool tab for viewport render parameters (pipeline, post process,
/// shadows, deferred SSAO/IBL). Runtime Tools stays diagnostics-only.
class EditorRenderSettingsTab : public UICompoundWidget
{
  public:
    EditorRenderSettingsTab(App* app = nullptr, IRenderSurfaceContext* presentSurface = nullptr);

    void onAttached() override;
    void tick(float deltaSeconds) override;

  protected:
    void construct() override;

  private:
    App* _app = nullptr;
    IRenderSurfaceContext* _presentSurface = nullptr;
    std::shared_ptr<RuntimeRenderSettingsSection> _settings;

    void refresh();
};

} // namespace ya
