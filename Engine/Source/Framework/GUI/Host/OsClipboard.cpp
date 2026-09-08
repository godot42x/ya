#include "GUI/Host/OsClipboard.h"

#include "Core/Os/Os.h"
#include "GUI/Widgets/WidgetTree.h"

namespace ya
{

void bindSdlClipboard(WidgetTree& tree)
{
    tree.setClipboardHooks(
        []() -> std::string { return Os::clipboardText(); },
        [](const std::string& text) { Os::setClipboardText(text); });
}

} // namespace ya
