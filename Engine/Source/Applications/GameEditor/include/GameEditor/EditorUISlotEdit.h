#pragma once

#include "Core/TypeIndex.h"

#include <memory>
#include <string_view>

namespace ya
{

struct UIElement;

/// The parent-owned slot of one designer widget, edited as its args struct.
/// The inspector reflects `args()`; `push()` writes that copy back with
/// `assign`, so the slot's setters keep their invariants (a stretched Auto
/// axis promotes to Fixed) and invalidate layout. The slot is looked up from
/// the child on every call because a reparent replaces the slot object.
class EditorUISlotEdit
{
  public:
    virtual ~EditorUISlotEdit() = default;

    /// nullptr without a parent slot, or for a slot type that has no authoring args.
    [[nodiscard]] static std::unique_ptr<EditorUISlotEdit> forChild(UIElement& child);

    [[nodiscard]] virtual type_index_t argsType() const = 0;
    [[nodiscard]] virtual void* args() = 0;
    [[nodiscard]] virtual std::string_view displayName() const = 0;
    /// Copy the slot into `args()`; false once the child has no slot of this type.
    virtual bool pull() = 0;
    /// Write `args()` into the slot; false once the child has no slot of this type.
    virtual bool push() = 0;
};

} // namespace ya
