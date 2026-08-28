#pragma once

#include "GUI/Widgets/CompoundWidget.h"
#include "GameEditor/UI/EditorAutoPropertySection.h"

#include <memory>

namespace ya
{

struct EditorLayer;
struct Entity;
struct WidgetTree;

/// Local retained composition for Inspector transform editing.
///
/// This is deliberately a tab-local compound: it owns only the transform
/// rows and their synchronization, not editor tab registration or workspace
/// lifetime.
class EditorTransformSection final : public UICompoundWidget
{
  public:
    EditorTransformSection(std::string name, EditorLayer& layer);

    void sync(WidgetTree& tree);
    [[nodiscard]] bool wantsTextInput(WidgetTree& tree) const;

  protected:
    void construct() override;

  private:
    EditorLayer* _layer = nullptr;
    Entity* _boundEntity = nullptr;
    std::shared_ptr<EditorAutoPropertySection> _autoSection;

    [[nodiscard]] std::shared_ptr<EditorAutoPropertySection> createAutoSection(Entity& entity) const;
};

} // namespace ya
