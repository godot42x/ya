#pragma once

#include "GUI/Widgets/UIElement.h"

#include <memory>
#include <string>

namespace ya
{

struct WidgetTree;

/// The open splash as a standalone page: one centered banner widget inside a
/// transparent, always-on-top overlay window. The window covers the display
/// while the project loads; only the banner widget is present to hit-test.
class EditorOpenSplashPage
{
  public:
    void build(const std::string& projectName, WidgetTree& tree);

  private:
    std::shared_ptr<UIElement> _banner;
};

} // namespace ya
