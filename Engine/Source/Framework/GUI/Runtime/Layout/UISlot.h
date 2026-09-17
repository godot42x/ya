#pragma once

#include "Core/Api.h"

#include <nlohmann/json.hpp>

namespace ya
{

struct UIElement;

/// Parent-owned child edge. A slot exists while its child belongs to its
/// visual parent; WidgetTree destroys the old edge before reparent/detach and
/// creates a new one under the destination parent.
class YA_GUI_API UISlot
{
public:
    UISlot(UIElement& parent, UIElement& child);
    virtual ~UISlot() = default;

    [[nodiscard]] UIElement& getParent() const { return *_parent; }
    [[nodiscard]] UIElement& getChild() const { return *_child; }
    virtual void appendRuntimeDiagnostics(nlohmann::json& node) const;
    virtual void serialize(nlohmann::json& node) const;
    virtual void deserialize(const nlohmann::json& node);
    [[nodiscard]] virtual bool isAutoSizeActive() const;

    /// Typed access remains open to user-defined UISlot subclasses; adding a
    /// slot type does not require editing an engine-owned enum.
    template <typename T>
    [[nodiscard]] T* as() { return dynamic_cast<T*>(this); }
    template <typename T>
    [[nodiscard]] const T* as() const { return dynamic_cast<const T*>(this); }

    /// Apply construct-time args. `TArgs::SlotType` names the slot class that
    /// accepts this payload, so a new slot type does not edit a central switch.
    /// Args stay aggregates so designated initializers keep working.
    template<typename TArgs>
    bool applyArgs(const TArgs& args)
    {
        auto* typed = as<typename TArgs::SlotType>();
        if (typed == nullptr) {
            return false;
        }
        typed->apply(args);
        return true;
    }

protected:
    void invalidateMeasure() const;
    void invalidateArrange() const;

private:
    UIElement* _parent = nullptr;
    UIElement* _child  = nullptr;
};

} // namespace ya
