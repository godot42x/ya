#include "GUI/Widgets/CompoundWidget.h"

namespace ya
{

void UICompoundWidget::prepareForAttach()
{
    if (_bConstructed) {
        return;
    }
    construct();
    _bConstructed = true;
}

} // namespace ya
