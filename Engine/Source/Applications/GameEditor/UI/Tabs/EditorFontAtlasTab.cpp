#include "GameEditor/UI/Tabs/EditorFontAtlasTab.h"

#include "GameEditor/UI/Shell/EditorTheme.h"
#include "GUI/Declarative/Build.h"
#include "GUI/Layout/UILayout.h"
#include "GUI/Widgets/Controls/Border.h"
#include "GUI/Widgets/Controls/ComboBox.h"
#include "GUI/Widgets/Controls/Image.h"
#include "GUI/Widgets/Controls/Overlay.h"
#include "GUI/Widgets/Controls/ScrollViewport.h"
#include "GUI/Widgets/Controls/SelectableRow.h"
#include "GUI/Widgets/Controls/SizeBox.h"
#include "GUI/Widgets/Controls/Text.h"
#include "GUI/Widgets/WidgetTree.h"
#include "RHI/Core/Texture.h"
#include "Render/Resources/FontManager.h"

#include <algorithm>
#include <cstdint>
#include <format>
#include <limits>
#include <vector>

namespace ya
{

namespace
{

constexpr const char* kFontAtlasChannelNames[4] = {"R", "G", "B", "A"};
constexpr float       kFontAtlasChannelCell     = 28.0f;

std::string fingerprintPages(const std::vector<FontManager::FFontAtlasDebugPage>& pages)
{
    std::string out = std::to_string(FontManager::get()->resourceRevision());
    out += ':';
    out += std::to_string(pages.size());
    for (const auto& page : pages) {
        out += '|';
        out += page.label;
        out += '@';
        out += std::to_string(reinterpret_cast<uintptr_t>(page.texture.get()));
        out += '#';
        out += std::to_string(page.glyphCount);
    }
    return out;
}

int indexOfLabel(const std::vector<FontManager::FFontAtlasDebugPage>& pages, std::string_view label)
{
    if (label.empty()) {
        return 0;
    }
    for (int i = 0; i < static_cast<int>(pages.size()); ++i) {
        if (pages[static_cast<size_t>(i)].label == label) {
            return i;
        }
    }
    return 0;
}

} // namespace

EditorFontAtlasTab::EditorFontAtlasTab()
    : UICompoundWidget("FontAtlasBody", "panel")
{
    enableTick();
}

void EditorFontAtlasTab::construct()
{
    auto combo = ui::comboBox("FontAtlasPageCombo")
                     .setStyleKey(editorStyle(StyleKey::ComboBox))
                     .setOnSelectionChanged([this](int index) {
                         _selectedIndex = index;
                         const auto pages = FontManager::get()->collectFontAtlasDebugPages();
                         if (index >= 0 && index < static_cast<int>(pages.size())) {
                             _selectedLabel = pages[static_cast<size_t>(index)].label;
                         }
                         bindPreview();
                     })
                     .share();
    _pageCombo = combo;

    auto detail = ui::text("FontAtlasDetail")
                      .setText("No font atlas pages")
                      .setStyleKey("text.muted")
                      .setWrap(true)
                      .share();
    _detailText = detail;

    // Atlas pages are shelf-packed from the top-left at texel size. Contain
    // letterboxes a 512 page into a short well and Nearest-minifies it — that
    // is the tiny centered crunchy look, not a bad sampler. Preview at 1:1.
    auto preview = std::make_shared<UIImage>("FontAtlasPreview");
    preview->setScaleMode(EImageScaleMode::Stretch);
    _preview = preview;

    auto previewFrame = ui::sizeBox("FontAtlasPreviewFrame")
                            .setWidth(1.0f)
                            .setHeight(1.0f)
                            .child(preview, ui::contentSlot().fill())
                            .share();
    _previewFrame = previewFrame;

    auto previewScroll =
        ui::scroll("FontAtlasScroll")
            .setAxis(EScrollAxis::Vertical)
            .child(ui::column("FontAtlasScrollContent")
                       .setSpacing(0.0f)
                       .child(previewFrame,
                              ui::boxSlot()
                                  .autoSize()
                                  .crossAlign(EUIBoxSlotCrossAlignment::Start))
                       .release(),
                   ui::contentSlot()
                       .hAlign(EUIOverlayAlignment::Start)
                       .vAlign(EUIOverlayAlignment::Start))
            .share();
    _previewScroll = previewScroll;
    // A page is texel-sized. The default vertical scroll squeezes that width
    // into the panel and Stretch then pulls the glyphs tall.
    _previewScroll->getScrollLayout().setCrossAxisUsesDesiredSize(true);

    auto channels = ui::row("FontAtlasChannels").setSpacing(editor_density::kControlSpacing);
    for (int channel = 0; channel < 4; ++channel) {
        const char* name = kFontAtlasChannelNames[channel];
        auto cell = ui::selectableRow(std::format("FontAtlasCh{}", name))
                        .setItemId(name)
                        .setSelected(_channelMask[static_cast<size_t>(channel)])
                        .setOnSelect([this, channel](const std::string&) {
                            _channelMask[static_cast<size_t>(channel)] =
                                !_channelMask[static_cast<size_t>(channel)];
                            syncChannelCells();
                            bindPreview();
                        })
                        .child(ui::text(std::format("FontAtlasCh{}Label", name))
                                   .setText(name)
                                   .setStyleKey("text.small")
                                   .setHAlign(EWidgetAlignH::Center)
                                   .setVAlign(EWidgetAlignV::Center),
                               ui::contentSlot().align(EUIOverlayAlignment::Center,
                                                       EUIOverlayAlignment::Center))
                        .share();
        _channels[static_cast<size_t>(channel)] = cell;
        channels.child(ui::sizeBox(std::format("FontAtlasCh{}Box", name))
                           .setWidth(kFontAtlasChannelCell)
                           .setHeight(kFontAtlasChannelCell)
                           .child(ui::overlay(std::format("FontAtlasCh{}Well", name))
                                      .child(ui::border(std::format("FontAtlasCh{}Fill", name))
                                                 .setStyleKey("panel.surface")
                                                 .setVisibility(EWidgetVisibility::HitTestInvisible),
                                             ui::overlaySlot().fill())
                                      .child(cell, ui::overlaySlot().fill())
                                      .release(),
                                  ui::contentSlot().fill())
                           .release(),
                       ui::boxSlot().preferredSize({kFontAtlasChannelCell, kFontAtlasChannelCell}));
    }

    auto well = ui::overlay("FontAtlasWell")
                    .child(ui::border("FontAtlasWellFill")
                               .setStyleKey("panel.canvas")
                               .setVisibility(EWidgetVisibility::HitTestInvisible),
                           ui::overlaySlot().fill())
                    .child(previewScroll, ui::overlaySlot().fill())
                    .release();

    const float comboH = editor_density::kToolbarHeight;
    addDetachedChild(ui::column("FontAtlasRoot")
                         .setSpacing(editor_density::kSectionSpacing)
                         .setPadding({12.0f, 12.0f})
                         .setStretchLastChild(true)
                         .child(combo,
                                ui::boxSlot()
                                    .preferredSize({0.0f, comboH})
                                    .maxSize({std::numeric_limits<float>::max(), comboH}))
                         .child(detail)
                         .child(channels.release(),
                                ui::boxSlot().preferredSize({0.0f, kFontAtlasChannelCell}))
                         .child(well, ui::boxSlot().fill())
                         .release());
}

void EditorFontAtlasTab::onAttached()
{
    refresh();
}

void EditorFontAtlasTab::tick(float)
{
    refresh();
}

void EditorFontAtlasTab::syncChannelCells()
{
    for (int channel = 0; channel < 4; ++channel) {
        if (_channels[static_cast<size_t>(channel)]) {
            _channels[static_cast<size_t>(channel)]->setSelected(
                _channelMask[static_cast<size_t>(channel)]);
        }
    }
}

void EditorFontAtlasTab::refresh()
{
    if (!_pageCombo || !_detailText || !_preview || !_previewFrame || !_previewScroll) {
        return;
    }
    const auto pages = FontManager::get()->collectFontAtlasDebugPages();
    const std::string fingerprint = fingerprintPages(pages);
    if (fingerprint == _fingerprint) {
        return;
    }
    _fingerprint = fingerprint;

    std::vector<std::string> items;
    items.reserve(pages.size());
    for (const auto& page : pages) {
        items.push_back(page.label);
    }
    _pageCombo->_items = std::move(items);
    _pageCombo->markPaintDirty();
    if (pages.empty()) {
        _selectedIndex = 0;
        _selectedLabel.clear();
        _pageCombo->setSelectedIndex(-1, false);
    }
    else {
        _selectedIndex = indexOfLabel(pages, _selectedLabel);
        _selectedLabel = pages[static_cast<size_t>(_selectedIndex)].label;
        _pageCombo->setSelectedIndex(_selectedIndex, false);
    }
    bindPreview();
}

void EditorFontAtlasTab::bindPreview()
{
    if (!_preview || !_detailText || !_previewFrame || !_previewScroll) {
        return;
    }
    const auto pages = FontManager::get()->collectFontAtlasDebugPages();
    if (pages.empty() || _selectedIndex < 0 || _selectedIndex >= static_cast<int>(pages.size())) {
        _detailText->setText("No font atlas pages");
        _preview->setTexture(nullptr);
        _preview->setResourceMissing(true);
        _preview->_tint = {1.0f, 1.0f, 1.0f, 1.0f};
        _preview->invalidateProperty(EUIPropertyImpact::Paint);
        _previewFrame->setWidthOverride(1.0f);
        _previewFrame->setHeightOverride(1.0f);
        _previewScroll->setScrollOffset(0.0f);
        _boundTexture = 0;
        return;
    }

    const FontManager::FFontAtlasDebugPage& page = pages[static_cast<size_t>(_selectedIndex)];
    _detailText->setText(page.detail.empty()
                             ? std::format("{} atlas page{}",
                                           pages.size(),
                                           pages.size() == 1 ? "" : "s")
                             : page.detail);

    const bool bR = _channelMask[0];
    const bool bG = _channelMask[1];
    const bool bB = _channelMask[2];
    const bool bA = _channelMask[3];
    const bool bSdf = page.renderMode == EFontRenderMode::SDF
                      || page.renderMode == EFontRenderMode::MSDF;
    if (!bR && !bG && !bB && bA) {
        _preview->_tint = {1.0f, 1.0f, 1.0f, 1.0f};
        _preview->setOpaqueSample(false);
    }
    else {
        _preview->_tint = {bR ? 1.0f : 0.0f, bG ? 1.0f : 0.0f, bB ? 1.0f : 0.0f, 1.0f};
        _preview->setOpaqueSample(bSdf || !bA);
    }
    _preview->invalidateProperty(EUIPropertyImpact::Paint);

    const uintptr_t textureId = reinterpret_cast<uintptr_t>(page.texture.get());
    const float     texW      = page.texture ? static_cast<float>(page.texture->getWidth()) : 1.0f;
    const float     texH      = page.texture ? static_cast<float>(page.texture->getHeight()) : 1.0f;
    // One texel per device pixel. The tree dpi turns logical points into
    // device pixels, so the frame is the texture size divided by that.
    float dpi = 1.0f;
    if (const WidgetTree* tree = getTree()) {
        if (tree->getDpiScale() > 0.0f) {
            dpi = tree->getDpiScale();
        }
    }
    _previewFrame->setWidthOverride(std::max(texW / dpi, 1.0f));
    _previewFrame->setHeightOverride(std::max(texH / dpi, 1.0f));
    if (textureId != _boundTexture) {
        _previewScroll->setScrollOffset(0.0f);
        _boundTexture = textureId;
    }

    if (page.texture) {
        _preview->setTexture(page.texture);
        _preview->setResourceMissing(false);
    }
    else {
        _preview->setTexture(nullptr);
        _preview->setResourceMissing(true);
    }
}

} // namespace ya
