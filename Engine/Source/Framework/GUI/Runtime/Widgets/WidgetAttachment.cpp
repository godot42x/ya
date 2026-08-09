#include "GUI/Widgets/WidgetAttachment.h"

#include "GUI/Widgets/WidgetTree.h"

namespace ya
{

void WidgetAttachment::detach()
{
    if (!tree) {
        return;
    }
    if (auto locked = widget.lock()) {
        if (locked->getTree() == tree) {
            tree->detach(*locked);
        }
    }
    tree = nullptr;
}

} // namespace ya
