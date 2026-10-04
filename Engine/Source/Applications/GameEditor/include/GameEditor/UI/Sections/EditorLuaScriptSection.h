#pragma once

#include "GUI/Widgets/CompoundWidget.h"
#include "ECS/Systems/ScriptPropertyPreview.h"

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace ya
{

struct EditorLayer;
struct UIContainer;
struct WidgetTree;

/// Retained LuaScriptComponent editor. Property rows come from
/// `scriptPropertyPreview()` when a Lua backend is installed.
class EditorLuaScriptSection final : public UICompoundWidget
{
  public:
    using ScriptPicker = std::function<void(std::string currentPath, std::function<void(std::string)> onPicked)>;

    EditorLuaScriptSection(std::string name,
                           EditorLayer& layer,
                           uint64_t entityUuid,
                           std::function<void()> onMutated = {},
                           ScriptPicker picker = {});

    void sync(WidgetTree& tree);

  protected:
    void construct() override;

  private:
    EditorLayer*             _layer = nullptr;
    uint64_t                 _entityUuid = 0;
    std::function<void()>    _onMutated;
    ScriptPicker             _picker;
    std::shared_ptr<UIContainer> _rows;
    std::vector<std::shared_ptr<UIElement>> _rowWidgets;
    std::string              _fingerprint;

    void noteMutated();
    void rebuildRows();
    [[nodiscard]] std::string fingerprint() const;
};

} // namespace ya
