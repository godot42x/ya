#include "GameEditor/UI/EditorDebugImagesTab.h"
#include "GameEditor/UI/EditorDebugCatalogView.h"

#include "GameEditor/EditorLayer.h"
#include "GUI/Declarative/Build.h"
#include "GUI/Widgets/Controls/CheckBox.h"
#include "GUI/Widgets/Controls/ComboBox.h"
#include "GUI/Widgets/Controls/Container.h"
#include "GUI/Widgets/Controls/Image.h"
#include "GUI/Widgets/Controls/Panel.h"
#include "GUI/Widgets/Controls/ScrollViewport.h"
#include "GUI/Widgets/Controls/Text.h"
#include "GUI/Widgets/WidgetTree.h"
#include "RHI/Core/Texture.h"

#include <format>

namespace ya
{
namespace
{
constexpr const char* kChannelNames[4] = {"R", "G", "B", "A"};
constexpr const char* kCubeFaces[6]    = {"PosX", "NegX", "PosY", "NegY", "PosZ", "NegZ"};
constexpr glm::vec2   kComboPreferred  = {0.0f, 26.0f};
constexpr glm::vec2   kPreviewPreferred = {0.0f, 180.0f};

std::shared_ptr<UICheckBox> makeLabeledCheckBox(const std::string& id, const char* label)
{
    auto box  = std::make_shared<UICheckBox>(id);
    auto text = std::make_shared<UIText>(id + "Label");
    text->setText(label);
    text->setFontSize(12);
    box->addDetachedChild(text);
    return box;
}

void attachChild(WidgetTree& tree, UIContainer& parent, const UIElementRef& child, glm::vec2 preferred = {})
{
    (void)tree.attach(parent, child);
    if ((preferred.x > 0.0f || preferred.y > 0.0f) && child) {
        if (auto* slot = parent.getBoxSlot(*child)) {
            slot->setPreferredSize(preferred);
        }
    }
}

std::string catalogFingerprint(const RenderViewportDebugCatalog& catalog)
{
    std::string out;
    for (const auto& category : catalog.categories) {
        out += category.id;
        out += '|';
    }
    out += '#';
    for (const auto& slot : catalog.slots) {
        out += slot.label;
        out += ';';
    }
    out += '#';
    for (const auto& group : catalog.groups) {
        out += group.label;
        out += ':';
        out += std::to_string(group.slotCount);
        out += ':';
        out += std::to_string(group.groupSize);
        out += ';';
    }
    return out;
}
} // namespace

EditorDebugImagesTab::EditorDebugImagesTab(EditorLayer& layer)
    : UICompoundWidget("DebugImagesBody", "panel.canvas")
    , _layer(&layer)
{
    enableTick();
}

void EditorDebugImagesTab::construct()
{
    auto category = ui::comboBox("DebugImagesCategory")
                        .setOnSelectionChanged([this](int index) {
                            _categoryFilter = index <= 0 ? -1 : index - 1;
                            _structureFingerprint.clear();
                        });
    _categoryCombo = category.share();

    auto status = ui::text("DebugImagesStatus").setFontSize(12).setStyleKey("text.muted");
    _statusText = status.share();

    _contentHost = ui::column("DebugImagesContent").setSpacing(10.0f).share();

    addDetachedChild(ui::panel("DebugImagesBodyInner")
        .setStyleKey("panel.canvas")
        .child(ui::scroll("DebugImagesScroll")
                   .setAxis(EScrollAxis::Vertical)
                   .child(ui::column("DebugImagesRoot")
                              .setSpacing(8.0f)
                              .setPadding({12.0f, 12.0f})
                              .child(ui::text("DebugImagesTitle")
                                         .setText("Debug Images")
                                         .setStyleKey("text.header")
                                         .setFontSize(14))
                              .child(std::move(category), ui::boxSlot().preferredSize({0.0f, 26.0f}))
                              .child(std::move(status))
                              .child(_contentHost, ui::boxSlot().fill()),
                          ui::overlaySlot().fill()),
               ui::canvasSlot().fill())
        .release());
}

void EditorDebugImagesTab::onAttached()
{
    refresh();
}

void EditorDebugImagesTab::tick(float)
{
    refresh();
}

void EditorDebugImagesTab::refresh()
{
    WidgetTree* tree = getTree();
    if (!tree) {
        return;
    }
    refreshFromTree(*tree);
}

void EditorDebugImagesTab::rebuild(WidgetTree& tree)
{
    for (const auto& child : std::vector<UIElementRef>(_contentHost->getChildren())) {
        if (child && child->isAttached()) {
            tree.detach(*child);
        }
    }
    _groups.clear();
    _slots.clear();
    if (!_layer || !_contentHost) {
        return;
    }

    const auto& catalog = _layer->getDebugCatalog();
    const auto  groupIndices = debugGroupIndicesForCategory(catalog, _categoryFilter);
    const auto  slotIndices  = debugStandaloneSlotIndices(catalog, _categoryFilter);

    if (!groupIndices.empty()) {
        auto header = std::make_shared<UIText>("DebugImagesGroupedHeader");
        header->setText("Grouped Views");
        header->setStyleKey("text.eyebrow");
        attachChild(tree, *_contentHost, header);
    }

    for (int groupIndex : groupIndices) {
        const auto& group     = catalog.groups[static_cast<size_t>(groupIndex)];
        const uint32_t slotCount = std::min(group.slotCount, static_cast<uint32_t>(catalog.slots.size()) - group.beginIndex);
        const uint32_t groupSize = std::max(1u, group.groupSize);
        const uint32_t groupCount = slotCount / groupSize;
        if (groupCount == 0) {
            continue;
        }

        FGroupRow row;
        row.groupIndex = groupIndex;
        auto label = std::make_shared<UIText>("DebugGroupLabel_" + std::to_string(groupIndex));
        label->setText(group.label);
        label->setFontSize(13);
        row.label = label;

        std::vector<std::string> groupItems;
        groupItems.reserve(groupCount);
        for (uint32_t item = 0; item < groupCount; ++item) {
            if (item < group.itemLabels.size() && !group.itemLabels[item].empty()) {
                groupItems.push_back(group.itemLabels[item]);
            }
            else if (group.type == RenderViewportDebugCatalog::EGroupType::CubeMapMipFaces) {
                groupItems.push_back(std::format("Mip {}", item));
            }
            else {
                groupItems.push_back(std::format("Viewer {}", item));
            }
        }
        auto groupCombo = std::make_shared<UIComboBox>("DebugGroupCombo_" + std::to_string(groupIndex));
        groupCombo->_items = std::move(groupItems);
        groupCombo->_selectedIndex = _layer->getDebugGroupSelectedIndex(groupIndex);
        groupCombo->_onSelectionChanged = [this, groupIndex](int index) {
            if (_layer) {
                _layer->setDebugGroupSelectedIndex(groupIndex, index);
            }
        };
        row.groupCombo = groupCombo;

        const uint32_t selectedGroup = static_cast<uint32_t>(std::max(0, _layer->getDebugGroupSelectedIndex(groupIndex)));
        std::vector<std::string> itemItems;
        itemItems.reserve(groupSize);
        const uint32_t slotBase = group.beginIndex + selectedGroup * groupSize;
        if (groupSize == 6 && (group.type == RenderViewportDebugCatalog::EGroupType::CubeMapFaces ||
                               group.type == RenderViewportDebugCatalog::EGroupType::CubeMapMipFaces)) {
            for (uint32_t face = 0; face < 6; ++face) {
                itemItems.emplace_back(kCubeFaces[face]);
            }
        }
        else {
            for (uint32_t offset = 0; offset < groupSize; ++offset) {
                const uint32_t slotIndex = slotBase + offset;
                itemItems.push_back(slotIndex < catalog.slots.size() ? catalog.slots[slotIndex].label
                                                                     : std::format("Slot {}", slotIndex));
            }
        }
        auto itemCombo = std::make_shared<UIComboBox>("DebugGroupItem_" + std::to_string(groupIndex));
        itemCombo->_items = std::move(itemItems);
        itemCombo->_selectedIndex = _layer->getDebugGroupItemSlot(groupIndex, selectedGroup);
        itemCombo->_onSelectionChanged = [this, groupIndex](int index) {
            if (!_layer) {
                return;
            }
            const uint32_t current = static_cast<uint32_t>(std::max(0, _layer->getDebugGroupSelectedIndex(groupIndex)));
            _layer->setDebugGroupItemSlot(groupIndex, current, index);
        };
        row.itemCombo = itemCombo;

        auto preview = std::make_shared<UIImage>("DebugGroupPreview_" + std::to_string(groupIndex));
        row.preview = preview;

        auto block = std::make_shared<UIContainer>("DebugGroupBlock_" + std::to_string(groupIndex));
        block->setSpacing(6.0f);
        attachChild(tree, *_contentHost, block);
        attachChild(tree, *block, label);
        attachChild(tree, *block, groupCombo, kComboPreferred);
        attachChild(tree, *block, itemCombo, kComboPreferred);
        attachChild(tree, *block, preview, kPreviewPreferred);
        _groups.push_back(std::move(row));
    }

    if (!slotIndices.empty()) {
        auto header = std::make_shared<UIText>("DebugImagesStandaloneHeader");
        header->setText("Standalone Views");
        header->setStyleKey("text.eyebrow");
        attachChild(tree, *_contentHost, header);
    }

    for (int slotIndex : slotIndices) {
        FSlotRow row;
        row.slotIndex = slotIndex;
        auto label = std::make_shared<UIText>("DebugSlotLabel_" + std::to_string(slotIndex));
        label->setText(catalog.slots[static_cast<size_t>(slotIndex)].label);
        label->setFontSize(13);
        row.label = label;

        auto maskRow = std::make_shared<UIContainer>("DebugSlotMask_" + std::to_string(slotIndex));
        maskRow->setDirection(EWidgetBoxLayout::Horizontal);
        maskRow->setSpacing(10.0f);
        const auto mask = _layer->getDebugChannelMask(static_cast<uint32_t>(slotIndex));
        for (int channel = 0; channel < 4; ++channel) {
            auto box = makeLabeledCheckBox("DebugSlotCh_" + std::to_string(slotIndex) + "_" + kChannelNames[channel],
                                           kChannelNames[channel]);
            box->setChecked(mask[static_cast<size_t>(channel)]);
            box->_onChanged = [this, slotIndex, channel](bool checked) {
                if (!_layer) {
                    return;
                }
                auto current = _layer->getDebugChannelMask(static_cast<uint32_t>(slotIndex));
                current[static_cast<size_t>(channel)] = checked;
                _layer->setDebugChannelMask(static_cast<uint32_t>(slotIndex), current);
            };
            row.channels[static_cast<size_t>(channel)] = box;
            attachChild(tree, *maskRow, box);
        }

        auto preview = std::make_shared<UIImage>("DebugSlotPreview_" + std::to_string(slotIndex));
        row.preview = preview;

        auto block = std::make_shared<UIContainer>("DebugSlotBlock_" + std::to_string(slotIndex));
        block->setSpacing(6.0f);
        attachChild(tree, *_contentHost, block);
        attachChild(tree, *block, label);
        attachChild(tree, *block, maskRow, {0.0f, 26.0f});
        attachChild(tree, *block, preview, kPreviewPreferred);
        _slots.push_back(std::move(row));
    }
}

void EditorDebugImagesTab::bindPreview(UIImage& image, std::shared_ptr<Texture>& cache, uint32_t slotIndex)
{
    if (!_layer) {
        return;
    }
    auto texture = _layer->getDebugSlotPreviewTexture(slotIndex);
    cache = texture;
    image.setTexture(texture);
    image.setResourceMissing(texture == nullptr);
}

void EditorDebugImagesTab::syncPreviews()
{
    if (!_layer) {
        return;
    }
    const auto& catalog = _layer->getDebugCatalog();
    for (auto& row : _groups) {
        if (row.groupIndex < 0 || row.groupIndex >= static_cast<int>(catalog.groups.size()) || !row.preview) {
            continue;
        }
        const auto& group = catalog.groups[static_cast<size_t>(row.groupIndex)];
        const uint32_t selectedGroup = static_cast<uint32_t>(std::max(0, _layer->getDebugGroupSelectedIndex(row.groupIndex)));
        const uint32_t selectedSlot  = static_cast<uint32_t>(std::max(0, _layer->getDebugGroupItemSlot(row.groupIndex, selectedGroup)));
        const uint32_t slotIndex     = debugGroupPreviewSlotIndex(group.beginIndex, std::max(1u, group.groupSize), selectedGroup, selectedSlot);
        row.previewSlot = slotIndex;
        if (row.groupCombo) {
            row.groupCombo->setSelectedIndex(_layer->getDebugGroupSelectedIndex(row.groupIndex), false);
        }
        if (row.itemCombo) {
            row.itemCombo->setSelectedIndex(_layer->getDebugGroupItemSlot(row.groupIndex, selectedGroup), false);
        }
        bindPreview(*row.preview, row.boundTexture, slotIndex);
    }
    for (auto& row : _slots) {
        if (row.slotIndex < 0 || !row.preview) {
            continue;
        }
        const auto mask = _layer->getDebugChannelMask(static_cast<uint32_t>(row.slotIndex));
        for (int channel = 0; channel < 4; ++channel) {
            if (row.channels[static_cast<size_t>(channel)]) {
                row.channels[static_cast<size_t>(channel)]->setChecked(mask[static_cast<size_t>(channel)]);
            }
        }
        bindPreview(*row.preview, row.boundTexture, static_cast<uint32_t>(row.slotIndex));
    }
}

void EditorDebugImagesTab::refreshFromTree(WidgetTree& tree)
{
    if (!_layer || !_contentHost || !_statusText) {
        return;
    }

    const auto& catalog = _layer->getDebugCatalog();
    if (_categoryCombo) {
        std::vector<std::string> items;
        items.emplace_back("All categories");
        for (const auto& category : catalog.categories) {
            items.push_back(category.label);
        }
        if (_categoryCombo->_items != items) {
            _categoryCombo->_items = std::move(items);
            _categoryCombo->setSelectedIndex(_categoryFilter < 0 ? 0 : _categoryFilter + 1, false);
        }
    }

    const auto groupIndices = debugGroupIndicesForCategory(catalog, _categoryFilter);
    const auto slotIndices  = debugStandaloneSlotIndices(catalog, _categoryFilter);
    if (groupIndices.empty() && slotIndices.empty()) {
        _statusText->setText("No debug images available for this category.");
    }
    else {
        _statusText->setText(std::format("Groups: {}  Standalone: {}", groupIndices.size(), slotIndices.size()));
    }

    std::string fingerprint = catalogFingerprint(catalog);
    fingerprint += "|cat:";
    fingerprint += std::to_string(_categoryFilter);
    for (int groupIndex : groupIndices) {
        fingerprint += "|g";
        fingerprint += std::to_string(groupIndex);
        fingerprint += ':';
        fingerprint += std::to_string(_layer->getDebugGroupSelectedIndex(groupIndex));
    }
    if (fingerprint != _structureFingerprint && _contentHost->isAttached()) {
        _structureFingerprint = std::move(fingerprint);
        rebuild(tree);
    }
    syncPreviews();
}

} // namespace ya
