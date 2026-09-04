#include "GameEditor/EditorLayerInternal.h"
#include "RHI/Core/Texture.h"
#include "RHI/Core/RenderResourceFactory.h"

namespace ya
{
const EditorViewportDebugCatalog& EditorLayer::getDebugCatalog() const
{
    static const EditorViewportDebugCatalog kEmptyCatalog;
    return _viewportCtx.debugCatalog ? *_viewportCtx.debugCatalog : kEmptyCatalog;
}

const RenderViewportDebugImageSlot* EditorLayer::getDebugSlotFrame(uint32_t slotIndex) const
{
    return slotIndex < _viewportCtx.debugImages.size() ? &_viewportCtx.debugImages[slotIndex] : nullptr;
}

void EditorLayer::ensureDebugViewerState()
{
    const auto& catalog = getDebugCatalog();
    if (catalog.slots.size() > _debugImageSlotStates.size()) {
        _debugImageSlotStates.resize(catalog.slots.size());
    }
    if (catalog.groups.size() > _debugGroupStates.size()) {
        _debugGroupStates.resize(catalog.groups.size());
    }
}

void EditorLayer::loadDebugGroupState(int groupIndex)
{
    ensureDebugViewerState();
    const auto& catalog = getDebugCatalog();
    if (groupIndex < 0 || groupIndex >= static_cast<int>(catalog.groups.size())) {
        return;
    }

    const auto& group = catalog.groups[static_cast<size_t>(groupIndex)];
    if (group.slotCount == 0 || group.beginIndex >= catalog.slots.size()) {
        return;
    }
    const uint32_t availableSlots = static_cast<uint32_t>(catalog.slots.size()) - group.beginIndex;
    const uint32_t slotCount      = std::min(group.slotCount, availableSlots);
    const uint32_t groupSize      = std::max(1u, group.groupSize);
    const uint32_t groupCount     = slotCount / groupSize;
    if (groupCount == 0) {
        return;
    }

    auto&             groupState = _debugGroupStates[static_cast<size_t>(groupIndex)];
    const std::string configKey  = buildDebugGroupConfigKey(group.label);
    if (groupState.configKey != configKey) {
        groupState.configKey          = configKey;
        groupState.selectedGroupIndex = 0;
        groupState.selectedSlots.clear();
        (void)ConfigManager::get().tryGet<int>("editor",
                                               buildDebugGroupSelectionConfigKey(group.label),
                                               groupState.selectedGroupIndex);
    }
    groupState.selectedGroupIndex = std::clamp(groupState.selectedGroupIndex, 0, static_cast<int>(groupCount) - 1);
    if (static_cast<uint32_t>(groupState.selectedSlots.size()) != groupCount) {
        groupState.selectedSlots.assign(groupCount, 0);
        for (uint32_t groupItemIndex = 0; groupItemIndex < groupCount; ++groupItemIndex) {
            int selectedSlot = 0;
            (void)ConfigManager::get().tryGet<int>("editor",
                                                   buildDebugGroupItemConfigKey(group.label, groupItemIndex),
                                                   selectedSlot);
            groupState.selectedSlots[groupItemIndex] = selectedSlot;
        }
    }
    for (int& selectedSlot : groupState.selectedSlots) {
        selectedSlot = std::clamp(selectedSlot, 0, static_cast<int>(groupSize) - 1);
    }
}

void EditorLayer::persistDebugGroupState(int groupIndex)
{
    const auto& catalog = getDebugCatalog();
    if (groupIndex < 0 || groupIndex >= static_cast<int>(catalog.groups.size())) {
        return;
    }
    const auto& group      = catalog.groups[static_cast<size_t>(groupIndex)];
    const auto& groupState = _debugGroupStates[static_cast<size_t>(groupIndex)];
    auto        configEditor = ConfigManager::Editor("editor");
    configEditor.set(buildDebugGroupSelectionConfigKey(group.label), groupState.selectedGroupIndex);
    for (uint32_t itemIndex = 0; itemIndex < groupState.selectedSlots.size(); ++itemIndex) {
        configEditor.set(buildDebugGroupItemConfigKey(group.label, itemIndex),
                         groupState.selectedSlots[itemIndex]);
    }
}

std::array<bool, 4> EditorLayer::getDebugChannelMask(uint32_t slotIndex)
{
    ensureDebugViewerState();
    const auto& catalog = getDebugCatalog();
    if (slotIndex >= catalog.slots.size()) {
        return {true, true, true, true};
    }
    auto& state = _debugImageSlotStates[slotIndex];
    syncDebugSlotState(catalog.slots[slotIndex], state);
    return state.channelEnabled;
}

void EditorLayer::setDebugChannelMask(uint32_t slotIndex, std::array<bool, 4> mask)
{
    ensureDebugViewerState();
    const auto& catalog = getDebugCatalog();
    if (slotIndex >= catalog.slots.size()) {
        return;
    }
    auto& state = _debugImageSlotStates[slotIndex];
    syncDebugSlotState(catalog.slots[slotIndex], state);
    if (state.channelEnabled == mask) {
        return;
    }
    state.channelEnabled = mask;
    ConfigManager::Editor("editor").set(state.configKey, state.channelEnabled);
    updateDebugSlotImageView(slotIndex, catalog.slots[slotIndex], state, true);
}

int EditorLayer::getDebugGroupSelectedIndex(int groupIndex)
{
    loadDebugGroupState(groupIndex);
    if (groupIndex < 0 || groupIndex >= static_cast<int>(_debugGroupStates.size())) {
        return 0;
    }
    return _debugGroupStates[static_cast<size_t>(groupIndex)].selectedGroupIndex;
}

void EditorLayer::setDebugGroupSelectedIndex(int groupIndex, int selectedIndex)
{
    loadDebugGroupState(groupIndex);
    if (groupIndex < 0 || groupIndex >= static_cast<int>(_debugGroupStates.size())) {
        return;
    }
    auto& state = _debugGroupStates[static_cast<size_t>(groupIndex)];
    if (state.selectedGroupIndex == selectedIndex) {
        return;
    }
    state.selectedGroupIndex = selectedIndex;
    loadDebugGroupState(groupIndex);
    persistDebugGroupState(groupIndex);
}

int EditorLayer::getDebugGroupItemSlot(int groupIndex, uint32_t itemIndex)
{
    loadDebugGroupState(groupIndex);
    if (groupIndex < 0 || groupIndex >= static_cast<int>(_debugGroupStates.size())) {
        return 0;
    }
    const auto& slots = _debugGroupStates[static_cast<size_t>(groupIndex)].selectedSlots;
    if (itemIndex >= slots.size()) {
        return 0;
    }
    return slots[itemIndex];
}

void EditorLayer::setDebugGroupItemSlot(int groupIndex, uint32_t itemIndex, int selectedSlot)
{
    loadDebugGroupState(groupIndex);
    if (groupIndex < 0 || groupIndex >= static_cast<int>(_debugGroupStates.size())) {
        return;
    }
    auto& slots = _debugGroupStates[static_cast<size_t>(groupIndex)].selectedSlots;
    if (itemIndex >= slots.size() || slots[itemIndex] == selectedSlot) {
        return;
    }
    slots[itemIndex] = selectedSlot;
    loadDebugGroupState(groupIndex);
    persistDebugGroupState(groupIndex);
}

std::shared_ptr<Texture> EditorLayer::getDebugSlotPreviewTexture(uint32_t slotIndex)
{
    ensureDebugViewerState();
    const auto& catalog = getDebugCatalog();
    if (slotIndex >= catalog.slots.size()) {
        return nullptr;
    }

    const auto& slot  = catalog.slots[slotIndex];
    auto&       state = _debugImageSlotStates[slotIndex];
    syncDebugSlotState(slot, state);
    updateDebugSlotImageView(slotIndex, slot, state, false);

    const auto* frame = getDebugSlotFrame(slotIndex);
    if (!frame || !frame->image) {
        state.previewTexture.reset();
        state.previewView = nullptr;
        return nullptr;
    }

    std::shared_ptr<IImageView> displayView;
    if (ImGuiHelper::IsIdentityRGBAChannelMask(state.channelEnabled)) {
        if (frame->ownedView) {
            displayView = frame->ownedView;
        }
        else {
            if (!state.identityView) {
                ImageViewCreateInfo ci;
                ci.label       = slot.label + "_preview";
                ci.viewType    = EImageViewType::View2D;
                ci.aspectFlags = slot.aspectFlags;
                auto* render          = _app ? _app->getRenderServices().getRender() : nullptr;
                auto* resourceFactory = render ? render->getResourceFactory() : nullptr;
                state.identityView    = resourceFactory ? resourceFactory->createImageView(frame->image, ci) : nullptr;
            }
            displayView = state.identityView;
        }
    }
    else {
        displayView = state.maskedView;
    }

    if (!displayView) {
        state.previewTexture.reset();
        state.previewView = nullptr;
        return nullptr;
    }
    if (state.previewTexture && state.previewView == displayView.get()) {
        return state.previewTexture;
    }
    state.previewView     = displayView.get();
    state.previewTexture  = Texture::wrap(frame->image, displayView, slot.label);
    return state.previewTexture;
}

void EditorLayer::syncDebugSlotState(const EditorViewportDebugCatalog::Slot& slot, ImageSlotState& state)
{
    const std::string configKey = buildDeferredMaskConfigKey(slot.label);
    if (state.configKey == configKey) {
        return;
    }

    auto&               configManager = ConfigManager::get();
    std::array<bool, 4> channelEnabled{true, true, true, true};
    (void)configManager.tryGet<std::array<bool, 4>>("editor", configKey, channelEnabled);

    state.configKey      = configKey;
    state.channelEnabled = channelEnabled;
    state.maskedView.reset();
    state.identityView.reset();
    state.previewTexture.reset();
    state.lastBase      = nullptr;
    state.previewView   = nullptr;
}

void EditorLayer::updateDebugSlotImageView(uint32_t slotIndex,
                                           const EditorViewportDebugCatalog::Slot& slot,
                                           ImageSlotState&                         state,
                                           bool                                    bForceRefresh)
{
    const auto* frame      = getDebugSlotFrame(slotIndex);
    const bool  baseChanged = frame && frame->defaultView != state.lastBase;
    if (!bForceRefresh && !baseChanged) {
        return;
    }

    state.lastBase = frame ? frame->defaultView : nullptr;
    state.identityView.reset();
    state.previewTexture.reset();
    state.previewView = nullptr;
    if (ImGuiHelper::IsIdentityRGBAChannelMask(state.channelEnabled) || !frame || !frame->image) {
        state.maskedView.reset();
        return;
    }

    ImageViewCreateInfo ci;
    ci.label         = slot.label + "_mask";
    ci.viewType      = EImageViewType::View2D;
    ci.aspectFlags   = slot.aspectFlags;
    ci.components    = ImGuiHelper::BuildRGBAChannelMaskMapping(state.channelEnabled);
    auto* const render          = _app ? _app->getRenderServices().getRender() : nullptr;
    auto* const resourceFactory = render ? render->getResourceFactory() : nullptr;
    state.maskedView            = resourceFactory ? resourceFactory->createImageView(frame->image, ci) : nullptr;
}

} // namespace ya
