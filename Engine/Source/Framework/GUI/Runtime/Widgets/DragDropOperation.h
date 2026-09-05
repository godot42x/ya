#pragma once

#include "Core/Common/Types.h"

#include <memory>
#include <string>
#include <string_view>

namespace ya
{

struct UIDragDropOperation;
using UIDragDropOperationRef = std::shared_ptr<UIDragDropOperation>;

/// In-flight drag session owned by WidgetTree (UE `FDragDropOperation` /
/// ImGui payload analog).
///
/// Extension is by subclass: typed fields live on the derived operation
/// (`FDockPanelDragDropOp`, `FTreeReorderDragDropOp`, ...). Drop targets
/// filter with `isType()` (cheap ImGui-style tag) then `as<T>()` for data.
/// The tree holds one operation for the session; it is not a string blob.
struct YA_GUI_API UIDragDropOperation
{
    std::string typeId;
    std::string ghostLabel;

    virtual ~UIDragDropOperation();

    [[nodiscard]] bool isType(std::string_view id) const { return typeId == id; }

    template <typename T>
    [[nodiscard]] const T* as() const
    {
        return dynamic_cast<const T*>(this);
    }

    template <typename T>
    [[nodiscard]] T* as()
    {
        return dynamic_cast<T*>(this);
    }
};

/// Convenience operation when the payload really is text / an opaque id.
/// Domain drags (dock panel, tree reorder) should subclass
/// `UIDragDropOperation` instead of stuffing fields into this string.
struct YA_GUI_API UIStringDragDropOperation : public UIDragDropOperation
{
    static constexpr const char* kTypeId = "text";
    std::string text;

    UIStringDragDropOperation() { typeId = kTypeId; }
    ~UIStringDragDropOperation() override;

    static UIDragDropOperationRef make(std::string text,
                                       std::string ghostLabel = {},
                                       std::string typeId = kTypeId);
};

} // namespace ya
