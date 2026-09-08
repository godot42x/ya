#pragma once

#include "Core/Api.h"

namespace ya
{

struct WidgetTree;

/// Bind the process OS clipboard to a WidgetTree. Closure tests leave the
/// default in-memory clipboard; windowed hosts call this after constructing
/// the tree.
YA_GUI_API void bindSdlClipboard(WidgetTree& tree);

} // namespace ya
