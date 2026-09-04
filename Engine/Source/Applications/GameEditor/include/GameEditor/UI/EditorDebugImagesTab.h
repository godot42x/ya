#pragma once

#include <array>
#include <memory>
#include <string>
#include <vector>

namespace ya
{

struct EditorLayer;
struct UICheckBox;
struct UIComboBox;
struct UIContainer;
struct UIElement;
struct UIImage;
struct UIText;
struct WidgetTree;
struct Texture;

/// Retained Debug Images tab. Catalog/mask/group selection stay on EditorLayer;
/// this tab is the WidgetTree view of that state.
class EditorDebugImagesTab
{
  public:
    explicit EditorDebugImagesTab(EditorLayer& layer) : _layer(&layer) {}

    [[nodiscard]] std::shared_ptr<UIElement> build(WidgetTree& tree);
    void sync(WidgetTree& tree);

  private:
    struct FSlotRow
    {
        int slotIndex = -1;
        std::shared_ptr<UIText> label;
        std::array<std::shared_ptr<UICheckBox>, 4> channels{};
        std::shared_ptr<UIImage> preview;
        std::shared_ptr<Texture> boundTexture;
    };

    struct FGroupRow
    {
        int groupIndex = -1;
        std::shared_ptr<UIText> label;
        std::shared_ptr<UIComboBox> groupCombo;
        std::shared_ptr<UIComboBox> itemCombo;
        std::shared_ptr<UIImage> preview;
        std::shared_ptr<Texture> boundTexture;
        uint32_t previewSlot = 0;
    };

    EditorLayer* _layer = nullptr;
    std::shared_ptr<UIComboBox> _categoryCombo;
    std::shared_ptr<UIText> _statusText;
    std::shared_ptr<UIContainer> _contentHost;
    std::vector<FGroupRow> _groups;
    std::vector<FSlotRow> _slots;
    std::string _structureFingerprint;
    int _categoryFilter = -1;

    void rebuild(WidgetTree& tree);
    void syncPreviews();
    void bindPreview(UIImage& image, std::shared_ptr<Texture>& cache, uint32_t slotIndex);
};
} // namespace ya
