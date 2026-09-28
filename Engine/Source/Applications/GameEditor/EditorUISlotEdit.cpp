#include "GameEditor/EditorUISlotEdit.h"

#include "GUI/Layout/UILayout.h"
#include "GUI/Widgets/UIElement.h"

namespace ya
{

namespace
{

template <typename TArgs>
class TSlotArgsEdit final : public EditorUISlotEdit
{
    using TSlot = typename TArgs::SlotType;

    UIElement*       _child = nullptr;
    std::string_view _displayName;
    TArgs            _args{};

  public:
    TSlotArgsEdit(UIElement& child, std::string_view displayName)
        : _child(&child)
        , _displayName(displayName)
    {
    }

    [[nodiscard]] type_index_t argsType() const override { return type_index_v<TArgs>; }
    [[nodiscard]] void* args() override { return &_args; }
    [[nodiscard]] std::string_view displayName() const override { return _displayName; }

    bool pull() override
    {
        TSlot* slot = resolve();
        if (!slot) {
            return false;
        }
        _args = slot->toArgs();
        return true;
    }

    bool push() override
    {
        TSlot* slot = resolve();
        if (!slot) {
            return false;
        }
        slot->assign(_args);
        return true;
    }

  private:
    [[nodiscard]] TSlot* resolve() const
    {
        UIElement* parent = _child->getParent();
        UISlot*    slot   = parent ? parent->getSlotForChild(*_child) : nullptr;
        return slot ? slot->as<TSlot>() : nullptr;
    }
};

template <typename TArgs>
std::unique_ptr<EditorUISlotEdit> tryEdit(UIElement& child, std::string_view displayName)
{
    auto edit = std::make_unique<TSlotArgsEdit<TArgs>>(child, displayName);
    if (!edit->pull()) {
        return nullptr;
    }
    return edit;
}

} // namespace

std::unique_ptr<EditorUISlotEdit> EditorUISlotEdit::forChild(UIElement& child)
{
    if (auto edit = tryEdit<FCanvasSlotArgs>(child, "Canvas Slot")) {
        return edit;
    }
    if (auto edit = tryEdit<FBoxSlotArgs>(child, "Box Slot")) {
        return edit;
    }
    if (auto edit = tryEdit<FOverlaySlotArgs>(child, "Overlay Slot")) {
        return edit;
    }
    if (auto edit = tryEdit<FContentSlotArgs>(child, "Content Slot")) {
        return edit;
    }
    return tryEdit<FTableSlotArgs>(child, "Table Slot");
}

} // namespace ya
