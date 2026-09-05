#pragma once

#include "GUI/Widgets/KeyedChildReconciler.h"

#include <filesystem>
#include <memory>
#include <string>

namespace ya
{

struct EditorLayer;
struct UIElement;
struct WidgetTree;

/// Retained Content Browser tab. FileExplorer owns mount/directory/filter
/// state; this tab is the WidgetTree view (keyed rows + visible window).
class EditorContentBrowserTab
{
  public:
    explicit EditorContentBrowserTab(EditorLayer& layer) : _layer(&layer) {}

    [[nodiscard]] std::shared_ptr<UIElement> build(WidgetTree& tree);
    void sync(WidgetTree& tree);

  private:
    EditorLayer* _layer = nullptr;

    std::shared_ptr<class FileExplorer> _explorer;
    std::shared_ptr<struct UIText> _pathText;
    std::shared_ptr<struct UITextField> _searchField;
    std::shared_ptr<struct UIContainer> _mountList;
    std::shared_ptr<struct UIContainer> _entryList;
    std::shared_ptr<struct UIContainer> _entryRows;
    std::shared_ptr<struct UISizeBox> _entryLeading;
    std::shared_ptr<struct UISizeBox> _entryTrailing;
    std::shared_ptr<struct UIScrollViewport> _entryScroll;
    std::unique_ptr<UIKeyedChildReconciler> _mountReconciler;
    std::unique_ptr<UIKeyedChildReconciler> _entryReconciler;
    std::string _fingerprint;
    float _entryScrollOffset = 0.0f;
    float _entryViewportHeight = 0.0f;
    bool _bRowsDirty = true;

    void rebuildRows(WidgetTree& tree);
    void selectMount(const std::string& itemId);
    void selectItem(const std::filesystem::path& path, bool bIsDirectory);
    void activateItem(const std::filesystem::path& path, bool bIsDirectory);
};

} // namespace ya
