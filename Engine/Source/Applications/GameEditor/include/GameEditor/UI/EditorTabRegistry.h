#pragma once

#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace ya
{

struct EditorLayer;
struct UIElement;
struct WidgetTree;

/// UE-style retained editor tab registry. Tabs own widget construction and
/// synchronization callbacks; EditorSurface only hosts the registry.
class EditorTabRegistry
{
  public:
    struct FTab
    {
        std::string id;
        std::string title;
        std::function<std::shared_ptr<UIElement>(EditorLayer&, WidgetTree&)> build;
        std::function<void(EditorLayer&, WidgetTree&)> sync;
    };

    void registerTab(FTab tab);
    [[nodiscard]] const std::vector<FTab>& tabs() const { return _tabs; }
    void clear() { _tabs.clear(); }

  private:
    std::vector<FTab> _tabs;
};

} // namespace ya
