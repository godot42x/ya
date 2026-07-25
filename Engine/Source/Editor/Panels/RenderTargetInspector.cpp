#include "Editor/Panels/RenderTargetInspector.h"

#include "Editor/ImGui/ImGuiHelper.h"
#include "Render/Core/RenderImage.h"
#include "Resource/Texture/TextureLibrary.h"
#include "Runtime/Application/App.h"
#include "Runtime/Rendering/RenderRuntime.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <format>
#include <string_view>

namespace ya
{

namespace
{

struct RenderTargetFormatOption
{
    std::string_view label;
    EFormat::T       format;
};

constexpr std::array<RenderTargetFormatOption, 3> kShadowDepthFormats = {{
    {"D16_UNORM", EFormat::D16_UNORM},
    {"D24_UNORM_S8_UINT", EFormat::D24_UNORM_S8_UINT},
    {"D32_SFLOAT", EFormat::D32_SFLOAT},
}};

constexpr std::array<RenderTargetFormatOption, 5> kColorFormats = {{
    {"R8G8B8A8_UNORM", EFormat::R8G8B8A8_UNORM},
    {"R8G8B8A8_SRGB", EFormat::R8G8B8A8_SRGB},
    {"R16G16B16A16_SFLOAT", EFormat::R16G16B16A16_SFLOAT},
    {"R32_SFLOAT", EFormat::R32_SFLOAT},
    {"B8G8R8A8_UNORM", EFormat::B8G8R8A8_UNORM},
}};

struct RenderTargetInspectorState
{
    int  selectedTargetIndex     = 0;
    int  selectedAttachmentIndex = 0;
    char targetSearch[64]        = {};
    char formatSearch[64]        = {};
};

int getColorAttachmentCount(const RenderTargetCatalog::Entry& entry)
{
    return static_cast<int>(entry.colorFormats.size());
}

bool hasDepthAttachment(const RenderTargetCatalog::Entry& entry)
{
    return entry.depthFormat.has_value();
}

EFormat::T getAttachmentFormat(const RenderTargetCatalog::Entry& entry, int attachmentIndex)
{
    const int colorCount = getColorAttachmentCount(entry);
    if (attachmentIndex >= 0 && attachmentIndex < colorCount) {
        return entry.colorFormats[attachmentIndex];
    }
    return entry.depthFormat.value_or(EFormat::Undefined);
}

bool containsInsensitive(std::string_view haystack, std::string_view needle)
{
    if (needle.empty()) {
        return true;
    }

    auto toLower = [](unsigned char value) { return static_cast<char>(std::tolower(value)); };
    std::string haystackLower(haystack.begin(), haystack.end());
    std::string needleLower(needle.begin(), needle.end());
    std::transform(haystackLower.begin(), haystackLower.end(), haystackLower.begin(), toLower);
    std::transform(needleLower.begin(), needleLower.end(), needleLower.begin(), toLower);
    return haystackLower.find(needleLower) != std::string::npos;
}

const char* formatLabel(EFormat::T format)
{
    for (const auto& option : kColorFormats) {
        if (option.format == format) return option.label.data();
    }
    for (const auto& option : kShadowDepthFormats) {
        if (option.format == format) return option.label.data();
    }
    return "Unknown";
}

bool isEntryInitialized(const RenderTargetCatalog::Entry& entry)
{
    return !entry.colorAttachments.empty() || entry.depthAttachment || entry.depthAttachmentView;
}

IImageView* getAttachmentImageView(const RenderTargetCatalog::Entry& entry, int attachmentIndex)
{
    if (attachmentIndex >= 0 && attachmentIndex < static_cast<int>(entry.colorAttachments.size())) {
        const auto& attachment = entry.colorAttachments[attachmentIndex];
        return attachment ? attachment->getImageView() : nullptr;
    }
    if (entry.depthAttachment) return entry.depthAttachment->getImageView();
    return entry.depthAttachmentView.get();
}

void requestFormat(RenderRuntime& runtime,
                   const RenderTargetCatalog::Entry& entry,
                   int attachmentIndex,
                   bool bDepth,
                   EFormat::T format)
{
    runtime.requestRenderTargetFormat({
        .attachment = bDepth ? RenderTargetFormatCommand::EAttachment::Depth : RenderTargetFormatCommand::EAttachment::Color,
        .owner = entry.owner,
        .colorAttachmentIndex = static_cast<uint32_t>(std::max(attachmentIndex, 0)),
        .format = format,
    });
}

} // namespace

void renderRenderTargetInspectorContent(App& app)
{
    static RenderTargetInspectorState state;
    auto* runtime = app.getRenderServices().getRenderRuntime();
    if (!runtime) {
        return;
    }

    const auto catalog = runtime->buildRenderTargetCatalog();
    const auto& entries = catalog.entries;
    if (entries.empty()) {
        ImGui::TextUnformatted("No render targets are available.");
        return;
    }

    ImGui::SetNextItemWidth(200.0f);
    ImGui::InputTextWithHint("##rt-search", "Search targets", state.targetSearch, sizeof(state.targetSearch));
    std::vector<int> filteredIndices;
    for (int index = 0; index < static_cast<int>(entries.size()); ++index) {
        if (containsInsensitive(entries[index].label, state.targetSearch)) filteredIndices.push_back(index);
    }
    if (filteredIndices.empty()) {
        ImGui::TextUnformatted("No render target matches the current search.");
        return;
    }

    if (state.selectedTargetIndex < 0 || state.selectedTargetIndex >= static_cast<int>(entries.size()) ||
        std::find(filteredIndices.begin(), filteredIndices.end(), state.selectedTargetIndex) == filteredIndices.end()) {
        state.selectedTargetIndex = filteredIndices.front();
        state.selectedAttachmentIndex = 0;
    }

    const auto& selectedEntry = entries[state.selectedTargetIndex];
    if (ImGui::BeginCombo("Target", selectedEntry.label)) {
        for (const int index : filteredIndices) {
            const bool bSelected = index == state.selectedTargetIndex;
            if (ImGui::Selectable(entries[index].label, bSelected)) {
                state.selectedTargetIndex = index;
                state.selectedAttachmentIndex = 0;
            }
            if (bSelected) ImGui::SetItemDefaultFocus();
        }
        ImGui::EndCombo();
    }

    if (!isEntryInitialized(selectedEntry)) {
        ImGui::TextUnformatted("Selected target is not initialized.");
        return;
    }

    const int colorCount = getColorAttachmentCount(selectedEntry);
    const bool bHasDepth = hasDepthAttachment(selectedEntry);
    const int attachmentCount = colorCount + (bHasDepth ? 1 : 0);
    state.selectedAttachmentIndex = std::clamp(state.selectedAttachmentIndex, 0, std::max(attachmentCount - 1, 0));
    const bool bEditingDepth = state.selectedAttachmentIndex >= colorCount && bHasDepth;
    const std::string attachmentLabel = bEditingDepth ? "Depth" : std::format("Color[{}]", state.selectedAttachmentIndex);
    if (attachmentCount > 0 && ImGui::BeginCombo("Attachment", attachmentLabel.c_str())) {
        for (int index = 0; index < attachmentCount; ++index) {
            const bool bSelected = index == state.selectedAttachmentIndex;
            const std::string label = index < colorCount ? std::format("Color[{}]", index) : "Depth";
            if (ImGui::Selectable(label.c_str(), bSelected)) state.selectedAttachmentIndex = index;
            if (bSelected) ImGui::SetItemDefaultFocus();
        }
        ImGui::EndCombo();
    }

    const EFormat::T currentFormat = attachmentCount > 0 ? getAttachmentFormat(selectedEntry, state.selectedAttachmentIndex) : EFormat::Undefined;
    ImGui::Text("Extent: %u x %u", selectedEntry.extent.width, selectedEntry.extent.height);
    ImGui::Text("Frame Buffers: %u", selectedEntry.frameBufferCount);
    ImGui::Text("Format: %s", formatLabel(currentFormat));

    if (selectedEntry.bSwapChainTarget) {
        ImGui::TextWrapped("Presentation target format and preview are owned by the swapchain.");
        return;
    }
    if (auto* imageView = getAttachmentImageView(selectedEntry, state.selectedAttachmentIndex)) {
        ImGuiHelper::Image(imageView, TextureLibrary::get().getDefaultSampler().get(), "Preview", ImVec2(256.0f, 256.0f));
    }
    if (!selectedEntry.bEditable) {
        return;
    }

    ImGui::SeparatorText("Attachment Format");
    ImGui::SetNextItemWidth(200.0f);
    ImGui::InputTextWithHint("##rt-format-search", "Search formats", state.formatSearch, sizeof(state.formatSearch));
    if (ImGui::BeginCombo(bEditingDepth ? "Depth Format" : "Color Format", formatLabel(currentFormat))) {
        const auto renderOptions = [&](const auto& formats)
        {
            for (const auto& option : formats) {
                if (!containsInsensitive(option.label, state.formatSearch)) continue;
                const bool bSelected = option.format == currentFormat;
                if (ImGui::Selectable(option.label.data(), bSelected)) {
                    requestFormat(*runtime, selectedEntry, state.selectedAttachmentIndex, bEditingDepth, option.format);
                }
                if (bSelected) ImGui::SetItemDefaultFocus();
            }
        };
        if (bEditingDepth) {
            renderOptions(kShadowDepthFormats);
        }
        else {
            renderOptions(kColorFormats);
        }
        ImGui::EndCombo();
    }
}

void renderRenderTargetInspector(App& app)
{
    auto* runtime = app.getRenderServices().getRenderRuntime();
    if (!runtime || !ImGui::Begin("Render Targets")) {
        if (runtime) ImGui::End();
        return;
    }
    renderRenderTargetInspectorContent(app);
    ImGui::End();
}

} // namespace ya
