#pragma once

#include "Core/Common/Types.h"

#include <memory>
#include <string>
#include <string_view>

namespace ya
{

struct UIDragDropOperation;
using UIDragDropOperationRef = std::shared_ptr<UIDragDropOperation>;

/// In-flight drag payload. Source-local ghost/observers live on WidgetTree;
/// the unique session identity (source vs hover window) lives on GUIDragRouter.
///
/// `payload` is the generic slot (id, path, text). Domain drags inherit and
/// add typed fields (`FDockPanelDragDropOp`, `FTreeReorderDragDropOp`, ...).
/// Drop targets filter with `isType()` then `as<T>()` for derived data.
struct YA_GUI_API UIDragDropOperation
{
    static constexpr const char* kTypeId = "payload";

    std::string typeId = kTypeId;
    std::string payload;
    std::string ghostLabel;
    /// Last tab of a closable extra window: hide the source OS window once
    /// the pointer leaves it so the window does not follow the cursor. Ghost
    /// stays on the desktop overlay / foreign tree. Cancel/NoTarget show it.
    bool bHideSourceWindowOnLeave = false;

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

    static UIDragDropOperationRef make(std::string payload,
                                         std::string ghostLabel = {},
                                         std::string typeId = {});
};

} // namespace ya
