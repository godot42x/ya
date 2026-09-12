#pragma once

#include "GUI/Widgets/CompoundWidget.h"

#include <array>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace ya
{

struct UIComboBox;
struct UIImage;
struct UIScrollViewport;
struct UISelectableRow;
struct UISizeBox;
struct UIText;
struct WidgetTree;

/// Window-tool debug tab: combo-picks one live FontManager atlas page and
/// previews it 1:1 from the top-left (with optional RGBA channel isolation).
class EditorFontAtlasTab : public UICompoundWidget
{
  public:
    EditorFontAtlasTab();

    void onAttached() override;
    void tick(float deltaSeconds) override;

  protected:
    void construct() override;

  private:
    std::shared_ptr<UIComboBox>       _pageCombo;
    std::shared_ptr<UIText>           _detailText;
    std::shared_ptr<UIImage>          _preview;
    std::shared_ptr<UISizeBox>        _previewFrame;
    std::shared_ptr<UIScrollViewport> _previewScroll;
    std::array<std::shared_ptr<UISelectableRow>, 4> _channels{};
    std::array<bool, 4>               _channelMask{true, true, true, true};
    std::string                       _fingerprint;
    std::string                       _selectedLabel;
    uintptr_t                         _boundTexture = 0;
    int                               _selectedIndex = 0;

    void refresh();
    void bindPreview();
    void syncChannelCells();
};

} // namespace ya
