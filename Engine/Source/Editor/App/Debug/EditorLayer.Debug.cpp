#include "Editor/App/EditorLayerInternal.h"

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
    state.lastBase      = nullptr;
}

bool EditorLayer::renderDebugSlotMaskControls(const EditorViewportDebugCatalog::Slot&, ImageSlotState& state)
{
    if (ImGuiHelper::RenderRGBAChannelMaskButtons(state.channelEnabled)) {
        ConfigManager::Editor("editor").set(state.configKey, state.channelEnabled);
        return true;
    }
    return false;
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
    if (ImGuiHelper::IsIdentityRGBAChannelMask(state.channelEnabled) || !frame || !frame->image) {
        state.maskedView.reset();
        return;
    }

    ImageViewCreateInfo ci;
    ci.label         = slot.label + "_mask";
    ci.viewType      = EImageViewType::View2D;
    ci.aspectFlags   = slot.aspectFlags;
    ci.components    = ImGuiHelper::BuildRGBAChannelMaskMapping(state.channelEnabled);
    auto* const render          = _app ? _app->getRender() : nullptr;
    auto* const resourceFactory = render ? render->getResourceFactory() : nullptr;
    state.maskedView            = resourceFactory ? resourceFactory->createImageView(frame->image, ci) : nullptr;
}

void EditorLayer::renderDebugSlotImage(uint32_t slotIndex,
                                       const EditorViewportDebugCatalog::Slot& slot,
                                       ImageSlotState&                         state,
                                       float                                   width,
                                       float                                   height,
                                       Sampler*                                sampler)
{
    const auto* frame = getDebugSlotFrame(slotIndex);
    IImageView* displayView = (ImGuiHelper::IsIdentityRGBAChannelMask(state.channelEnabled) || !state.maskedView)
                                ? (frame ? frame->defaultView : nullptr)
                                : state.maskedView.get();
    ImGuiHelper::Image(displayView,
                       sampler,
                       slot.label,
                       ImVec2(width, height),
                       ImVec2(0, 0),
                       ImVec2(1, 1),
                       {slot.tint.x, slot.tint.y, slot.tint.z, slot.tint.w});
}

bool EditorLayer::renderDebugImageGroup(const EditorViewportDebugCatalog::Group& group,
                                        int                                           groupIndex,
                                        const ImVec2&                                 panelSize,
                                        bool                                          bUseCollapsingHeader,
                                        float                                         maxPreviewSize)
{
    using namespace ImGui;
    Sampler* sampler = TextureLibrary::get().getLinearSampler();
    const auto& catalog = getDebugCatalog();
    const auto& slots   = catalog.slots;
    const auto& groups  = catalog.groups;

    if (slots.size() > _debugImageSlotStates.size()) {
        _debugImageSlotStates.resize(slots.size());
    }
    if (groups.size() > _debugGroupStates.size()) {
        _debugGroupStates.resize(groups.size());
    }

    if (group.slotCount == 0 || group.beginIndex >= slots.size()) {
        return false;
    }

    const uint32_t availableSlots = static_cast<uint32_t>(slots.size()) - group.beginIndex;
    const uint32_t slotCount      = std::min(group.slotCount, availableSlots);
    if (slotCount == 0) {
        return false;
    }

    const uint32_t groupSize  = std::max(1u, group.groupSize);
    const uint32_t groupCount = slotCount / groupSize;
    if (groupCount == 0) {
        return false;
    }

    auto&             groupState = _debugGroupStates[groupIndex];
    const std::string configKey  = buildDebugGroupConfigKey(group.label);
    if (groupState.configKey != configKey) {
        groupState.configKey          = configKey;
        groupState.selectedGroupIndex = 0;
        groupState.selectedSlots.clear();
        (void)ConfigManager::get().tryGet<int>("editor", buildDebugGroupSelectionConfigKey(group.label), groupState.selectedGroupIndex);
    }

    groupState.selectedGroupIndex = std::clamp(groupState.selectedGroupIndex, 0, static_cast<int>(groupCount) - 1);

    if (static_cast<uint32_t>(groupState.selectedSlots.size()) != groupCount) {
        groupState.selectedSlots.assign(groupCount, 0);
        for (uint32_t groupItemIndex = 0; groupItemIndex < groupCount; ++groupItemIndex) {
            int               selectedSlot = 0;
            const std::string key          = buildDebugGroupItemConfigKey(group.label, groupItemIndex);
            (void)ConfigManager::get().tryGet<int>("editor", key, selectedSlot);
            groupState.selectedSlots[groupItemIndex] = selectedSlot;
        }
    }

    if (bUseCollapsingHeader) {
        if (!CollapsingHeader(group.label.c_str(), ImGuiTreeNodeFlags_DefaultOpen)) {
            return false;
        }
    }
    else {
        TextUnformatted(group.label.c_str());
        Separator();
    }

    auto renderCubeFaceSelector = [&](int& selectedFace, bool& anySelectionChanged) {
        for (uint32_t rowIndex = 0; rowIndex < 2; ++rowIndex) {
            float totalSpacing = GetStyle().ItemSpacing.x * 2.0f;
            float buttonWidth  = (GetContentRegionAvail().x - totalSpacing) / 3.0f;
            for (uint32_t columnIndex = 0; columnIndex < 3; ++columnIndex) {
                const uint32_t faceIndex   = rowIndex * 3 + columnIndex;
                const bool     bSelected   = (selectedFace == static_cast<int>(faceIndex));
                ImVec4         buttonColor = bSelected ? ImVec4(0.22f, 0.58f, 0.98f, 0.95f) : ImVec4(0.18f, 0.20f, 0.24f, 0.85f);
                ImVec4         hoverColor  = bSelected ? ImVec4(0.30f, 0.66f, 1.00f, 1.00f) : ImVec4(0.24f, 0.27f, 0.32f, 0.95f);
                ImVec4         activeColor = bSelected ? ImVec4(0.16f, 0.48f, 0.88f, 1.00f) : ImVec4(0.20f, 0.23f, 0.28f, 1.00f);

                ya::ImGuiStyleScope buttonStyle;
                buttonStyle.pushColor(ImGuiCol_Button, buttonColor);
                buttonStyle.pushColor(ImGuiCol_ButtonHovered, hoverColor);
                buttonStyle.pushColor(ImGuiCol_ButtonActive, activeColor);
                if (Button(kCubeFaceLabels[faceIndex], ImVec2(buttonWidth, 0.0f))) {
                    selectedFace        = static_cast<int>(faceIndex);
                    anySelectionChanged = true;
                }

                if (columnIndex < 2) {
                    SameLine();
                }
            }
        }
    };

    auto renderSlotViewer = [&](uint32_t slotIndex) {
        const auto& slot  = slots[slotIndex];
        auto& state = _debugImageSlotStates[slotIndex];
        syncDebugSlotState(slot, state);
        bool maskChanged = renderDebugSlotMaskControls(slot, state);
        updateDebugSlotImageView(slotIndex, slot, state, maskChanged);

        const float availableWidth = GetContentRegionAvail().x;
        const float viewerWidth    = availableWidth;
        const float viewerHeight   = maxPreviewSize > 0.0f ? std::min(viewerWidth, maxPreviewSize) : std::min(viewerWidth, panelSize.x);
        renderDebugSlotImage(slotIndex, slot, state, viewerWidth, viewerHeight, sampler);
    };

    PushID(group.label.c_str());
    bool anySelectionChanged = false;

    if (group.type == EditorViewportDebugCatalog::EGroupType::CubeMapMipFaces && groupSize == CubeFace_Count) {
        std::string comboItems;
        for (uint32_t groupItemIndex = 0; groupItemIndex < groupCount; ++groupItemIndex) {
            if (groupItemIndex < group.itemLabels.size() && !group.itemLabels[groupItemIndex].empty()) {
                comboItems += group.itemLabels[groupItemIndex];
            }
            else {
                comboItems += std::format("Mip {}", groupItemIndex);
            }
            comboItems.push_back('\0');
        }
        comboItems.push_back('\0');

        if (Combo("Mip", &groupState.selectedGroupIndex, comboItems.c_str())) {
            anySelectionChanged = true;
        }

        const uint32_t selectedGroup = static_cast<uint32_t>(groupState.selectedGroupIndex);
        const uint32_t slotBase      = group.beginIndex + selectedGroup * groupSize;
        int&           selectedFace  = groupState.selectedSlots[selectedGroup];
        selectedFace                 = std::clamp(selectedFace, 0, static_cast<int>(groupSize) - 1);

        renderCubeFaceSelector(selectedFace, anySelectionChanged);
        renderSlotViewer(slotBase + static_cast<uint32_t>(selectedFace));
    }
    else {
        const int viewerColumns = std::max(1, std::min(2, static_cast<int>(groupCount)));
        if (BeginTable("DebugGroupViewerTable", viewerColumns, ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_SizingStretchSame)) {
            for (int columnIndex = 0; columnIndex < viewerColumns; ++columnIndex) {
                TableSetupColumn("", ImGuiTableColumnFlags_WidthStretch, 1.0f);
            }

            for (uint32_t groupItemIndex = 0; groupItemIndex < groupCount; ++groupItemIndex) {
                if (groupItemIndex % static_cast<uint32_t>(viewerColumns) == 0) {
                    TableNextRow();
                }
                TableSetColumnIndex(static_cast<int>(groupItemIndex % static_cast<uint32_t>(viewerColumns)));
                PushID(static_cast<int>(groupItemIndex));

                const uint32_t slotBase     = group.beginIndex + groupItemIndex * groupSize;
                int&           selectedFace = groupState.selectedSlots[groupItemIndex];
                selectedFace                = std::clamp(selectedFace, 0, static_cast<int>(groupSize) - 1);

                if (groupItemIndex < group.itemLabels.size() && !group.itemLabels[groupItemIndex].empty()) {
                    TextUnformatted(group.itemLabels[groupItemIndex].c_str());
                }
                else {
                    Text("Viewer %u", groupItemIndex);
                }

                if (group.type == EditorViewportDebugCatalog::EGroupType::CubeMapFaces && groupSize == CubeFace_Count) {
                    renderCubeFaceSelector(selectedFace, anySelectionChanged);
                }
                else {
                    std::string comboItems;
                    for (uint32_t slotOffset = 0; slotOffset < groupSize; ++slotOffset) {
                        comboItems += slots[slotBase + slotOffset].label;
                        comboItems.push_back('\0');
                    }
                    comboItems.push_back('\0');
                    if (Combo("Viewer", &selectedFace, comboItems.c_str())) {
                        anySelectionChanged = true;
                    }
                }

                renderSlotViewer(slotBase + static_cast<uint32_t>(selectedFace));
                PopID();
            }

            EndTable();
        }
    }

    if (anySelectionChanged) {
        auto configEditor = ConfigManager::Editor("editor");
        configEditor.set(buildDebugGroupSelectionConfigKey(group.label), groupState.selectedGroupIndex);
        for (uint32_t groupItemIndex = 0; groupItemIndex < groupCount; ++groupItemIndex) {
            configEditor.set(buildDebugGroupItemConfigKey(group.label, groupItemIndex),
                             groupState.selectedSlots[groupItemIndex]);
        }
    }

    PopID();
    return true;
}

void EditorLayer::renderDebugImageGroups(const ImVec2& panelSize, int categoryFilter)
{
    const auto& groups = getDebugCatalog().groups;
    if (groups.empty()) {
        return;
    }

    for (int groupIndex = 0; groupIndex < static_cast<int>(groups.size()); ++groupIndex) {
        const auto& group = groups[groupIndex];
        if (categoryFilter >= 0 && static_cast<int>(group.categoryIndex) != categoryFilter) {
            continue;
        }
        renderDebugImageGroup(group, groupIndex, panelSize, true, 0.0f);
    }
}

void EditorLayer::renderDebugImageGroupsGrid(const ImVec2& panelSize, int categoryFilter, float maxPreviewSize)
{
    using namespace ImGui;
    const auto& groups = getDebugCatalog().groups;
    if (groups.empty()) {
        return;
    }

    std::vector<int> groupIndices;
    groupIndices.reserve(groups.size());
    for (int groupIndex = 0; groupIndex < static_cast<int>(groups.size()); ++groupIndex) {
        if (categoryFilter >= 0 && static_cast<int>(groups[groupIndex].categoryIndex) != categoryFilter) {
            continue;
        }
        groupIndices.push_back(groupIndex);
    }

    if (groupIndices.empty()) {
        return;
    }

    const int columnCount = std::max(1, std::min(3, static_cast<int>(panelSize.x / 360.0f)));
    if (!BeginTable("DebugGroupGrid",
                    columnCount,
                    ImGuiTableFlags_SizingStretchSame | ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_BordersInnerH)) {
        return;
    }

    for (int columnIndex = 0; columnIndex < columnCount; ++columnIndex) {
        TableSetupColumn("", ImGuiTableColumnFlags_WidthStretch, 1.0f);
    }

    for (int filteredIndex = 0; filteredIndex < static_cast<int>(groupIndices.size()); ++filteredIndex) {
        if (filteredIndex % columnCount == 0) {
            TableNextRow();
        }
        TableSetColumnIndex(filteredIndex % columnCount);

        const int groupIndex = groupIndices[filteredIndex];
        PushID(groupIndex);
        renderDebugImageGroup(groups[groupIndex], groupIndex, ImVec2(GetContentRegionAvail().x, panelSize.y), false, maxPreviewSize);
        PopID();
    }

    EndTable();
}


void EditorLayer::renderDebugImageSlots(const ImVec2& panelSize, int categoryFilter)
{
    using namespace ImGui;
    Sampler*    sampler = TextureLibrary::get().getLinearSampler();
    const auto& catalog = getDebugCatalog();
    const auto& slots   = catalog.slots;
    const auto& groups  = catalog.groups;
    std::vector<bool> groupedSlotMask(slots.size(), false);
    for (const auto& group : groups) {
        if (categoryFilter >= 0 && static_cast<int>(group.categoryIndex) != categoryFilter) {
            continue;
        }

        const uint32_t slotEnd = std::min<uint32_t>(group.beginIndex + group.slotCount, static_cast<uint32_t>(slots.size()));
        for (uint32_t slotIndex = group.beginIndex; slotIndex < slotEnd; ++slotIndex) {
            groupedSlotMask[slotIndex] = true;
        }
    }

    std::vector<int> filteredSlotIndices;
    filteredSlotIndices.reserve(slots.size());
    for (int slotIndex = 0; slotIndex < static_cast<int>(slots.size()); ++slotIndex) {
        if (groupedSlotMask[slotIndex]) {
            continue;
        }
        if (categoryFilter >= 0 && static_cast<int>(slots[slotIndex].categoryIndex) != categoryFilter) {
            continue;
        }
        filteredSlotIndices.push_back(slotIndex);
    }

    const int totalSlots = static_cast<int>(filteredSlotIndices.size());
    if (totalSlots <= 0) {
        return;
    }

    if (static_cast<int>(slots.size()) > static_cast<int>(_debugImageSlotStates.size())) {
        _debugImageSlotStates.resize(slots.size());
    }

    const int rowSize = std::max(1, std::min(4, totalSlots));

    for (const int slotIndex : filteredSlotIndices) {
        syncDebugSlotState(slots[slotIndex], _debugImageSlotStates[slotIndex]);
    }

    if (!ImGui::BeginTable("DebugImageGrid", rowSize, ImGuiTableFlags_BordersInnerV)) {
        return;
    }

    for (int i = 0; i < rowSize; ++i) {
        ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthStretch, 1.0f);
    }

    const float padding   = ImGui::GetStyle().ItemSpacing.x;
    const float colWidth  = (panelSize.x - padding * static_cast<float>(rowSize - 1)) / static_cast<float>(rowSize);
    const float imgHeight = colWidth;

    for (int filteredIndex = 0; filteredIndex < totalSlots; ++filteredIndex) {
        if (filteredIndex % rowSize == 0) {
            TableNextRow();
        }

        const int columnIndex = filteredIndex % rowSize;
        TableSetColumnIndex(columnIndex);

        const int  slotIndex = filteredSlotIndices[filteredIndex];
        const auto& slot     = slots[slotIndex];
        auto&       state    = _debugImageSlotStates[slotIndex];
        Text("%s", slot.label.c_str());
        ImGui::PushID(slotIndex);
        bool maskChanged = renderDebugSlotMaskControls(slot, state);
        updateDebugSlotImageView(slotIndex, slot, state, maskChanged);
        renderDebugSlotImage(slotIndex, slot, state, colWidth, imgHeight, sampler);
        ImGui::PopID();
    }

    ImGui::EndTable();
}

// MARK: Debug
void EditorLayer::debugWindow()
{
    if (!ImGui::Begin("Debug Window"))
    {
        ImGui::End();
        return;
    }

    const ImVec2 panelSize = ImGui::GetContentRegionAvail();
    const auto&  catalog    = getDebugCatalog();
    const auto&  categories = catalog.categories;
    const auto&  slots      = catalog.slots;
    const auto&  groups     = catalog.groups;

    auto renderCategoryContent = [&](int categoryIndex) {
        int groupCount = 0;
        int standaloneSlotCount = 0;

        std::vector<bool> groupedSlotMask(slots.size(), false);
        for (const auto& group : groups) {
            if (categoryIndex >= 0 && static_cast<int>(group.categoryIndex) != categoryIndex) {
                continue;
            }
            ++groupCount;
            const uint32_t slotEnd = std::min<uint32_t>(group.beginIndex + group.slotCount, static_cast<uint32_t>(slots.size()));
            for (uint32_t slotIndex = group.beginIndex; slotIndex < slotEnd; ++slotIndex) {
                groupedSlotMask[slotIndex] = true;
            }
        }

        for (int slotIndex = 0; slotIndex < static_cast<int>(slots.size()); ++slotIndex) {
            if (groupedSlotMask[slotIndex]) {
                continue;
            }
            if (categoryIndex >= 0 && static_cast<int>(slots[slotIndex].categoryIndex) != categoryIndex) {
                continue;
            }
            ++standaloneSlotCount;
        }

        ImGui::Text("Groups: %d", groupCount);
        ImGui::SameLine();
        ImGui::Text("Standalone: %d", standaloneSlotCount);
        ImGui::Separator();

        if (groupCount > 0) {
            ImGui::SeparatorText("Grouped Views");
            renderDebugImageGroupsGrid(panelSize, categoryIndex, 280.0f);
        }

        if (standaloneSlotCount > 0) {
            ImGui::SeparatorText("Standalone Views");
            renderDebugImageSlots(panelSize, categoryIndex);
        }

        if (groupCount == 0 && standaloneSlotCount == 0) {
            ImGui::TextDisabled("No debug images available for this category.");
        }
    };

    if (categories.empty()) {
        renderCategoryContent(-1);
        ImGui::End();
        return;
    }

    if (ImGui::BeginTabBar("DebugCategories")) {
        for (int categoryIndex = 0; categoryIndex < static_cast<int>(categories.size()); ++categoryIndex) {
            const auto& category = categories[categoryIndex];
            if (!ImGui::BeginTabItem(category.label.c_str())) {
                continue;
            }

            renderCategoryContent(categoryIndex);
            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
    }

    ImGui::End();
}
} // namespace ya
