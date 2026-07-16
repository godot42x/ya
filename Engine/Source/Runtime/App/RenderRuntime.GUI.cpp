#include "RenderRuntime.h"

#include "DeferredRender/DeferredRenderPipeline.h"
#include "ImGuiHelper.h"
#include "Resource/Texture/TextureLibrary.h"
#include "Runtime/App/ForwardRender/ForwardRenderPipeline.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <string_view>

namespace ya
{

namespace
{

template <typename Fn>
void drawSettingsSection(const char* label, Fn&& body)
{
    if (!ImGui::TreeNode(label)) {
        return;
    }
    body();
    ImGui::TreePop();
}

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

int getEntryColorAttachmentCount(const RenderTargetEditorCatalog::Entry& entry)
{
    return static_cast<int>(entry.colorFormats.size());
}

bool entryHasDepthAttachment(const RenderTargetEditorCatalog::Entry& entry)
{
    return entry.depthFormat.has_value();
}

EFormat::T getEntryAttachmentFormat(const RenderTargetEditorCatalog::Entry& entry, int attachmentIndex)
{
    const int colorCount = getEntryColorAttachmentCount(entry);
    if (attachmentIndex < colorCount) {
        if (attachmentIndex >= 0 && attachmentIndex < static_cast<int>(entry.colorFormats.size())) {
            return entry.colorFormats[attachmentIndex];
        }
        return EFormat::Undefined;
    }

    if (entry.depthFormat.has_value()) {
        return *entry.depthFormat;
    }
    return EFormat::Undefined;
}

bool containsInsensitive(std::string_view haystack, std::string_view needle)
{
    if (needle.empty()) {
        return true;
    }

    auto toLower = [](unsigned char value)
    {
        return static_cast<char>(std::tolower(value));
    };

    std::string haystackLower(haystack.begin(), haystack.end());
    std::string needleLower(needle.begin(), needle.end());
    std::transform(haystackLower.begin(), haystackLower.end(), haystackLower.begin(), toLower);
    std::transform(needleLower.begin(), needleLower.end(), needleLower.begin(), toLower);
    return haystackLower.find(needleLower) != std::string::npos;
}

const char* formatLabel(EFormat::T format)
{
    for (const auto& option : kColorFormats) {
        if (option.format == format) {
            return option.label.data();
        }
    }
    for (const auto& option : kShadowDepthFormats) {
        if (option.format == format) {
            return option.label.data();
        }
    }

    return "Unknown";
}

bool isEntryInitialized(const RenderTargetEditorCatalog::Entry& entry)
{
    return !entry.colorAttachments.empty() || entry.depthAttachment || entry.depthAttachmentView;
}

bool isSwapChainEntry(const RenderTargetEditorCatalog::Entry& entry)
{
    return entry.bSwapChainTarget;
}

Extent2D getEntryExtent(const RenderTargetEditorCatalog::Entry& entry)
{
    return entry.extent;
}

uint32_t getEntryFrameBufferCount(const RenderTargetEditorCatalog::Entry& entry)
{
    return entry.frameBufferCount;
}

IImageView* getAttachmentImageView(const RenderTargetEditorCatalog::Entry& entry, int attachmentIndex)
{
    if (attachmentIndex >= 0 && attachmentIndex < static_cast<int>(entry.colorAttachments.size())) {
        const auto& attachment = entry.colorAttachments[attachmentIndex];
        return attachment ? attachment->getImageView() : nullptr;
    }
    if (attachmentIndex >= static_cast<int>(entry.colorAttachments.size()) && entry.depthAttachment) {
        return entry.depthAttachment->getImageView();
    }
    if (attachmentIndex >= static_cast<int>(entry.colorAttachments.size()) && entry.depthAttachmentView) {
        return entry.depthAttachmentView.get();
    }
    return nullptr;
}

} // namespace

void RenderRuntime::renderWorldSettingsGUI()
{
    if (auto* pipeline = getActivePipelineSettingsUI(); pipeline && ImGui::TreeNode("General")) {
        pipeline->renderGeneralSettingsGUI();
        ImGui::TreePop();
    }

    if (auto* pipeline = getActivePipelineSettingsUI(); isDeferredPipelineActive() && pipeline && ImGui::TreeNode("Lighting")) {
        pipeline->renderLightingSettingsGUI();
        ImGui::TreePop();
    }

    if (auto* pipeline = getActivePipelineSettingsUI(); isDeferredPipelineActive() && pipeline && ImGui::TreeNode("Ambient Occlusion")) {
        pipeline->renderAOSettingsGUI();
        ImGui::TreePop();
    }

    if (auto* pipeline = getActivePipelineSettingsUI(); pipeline && ImGui::TreeNode("Shadows")) {
        pipeline->renderShadowSettingsGUI();
        ImGui::TreePop();
    }

    if (auto* pipeline = getActivePipelineSettingsUI(); pipeline && ImGui::TreeNode("Post Process")) {
        pipeline->renderPostProcessSettingsGUI();
        ImGui::TreePop();
    }
}

void RenderRuntime::renderProfilingDetailsGUI()
{
    auto* pipeline = getActivePipelineDebugUI();
    if (!pipeline) {
        return;
    }

    if (ImGui::TreeNode("Runtime Perf")) {
        pipeline->renderPerformanceGUI();
        ImGui::TreePop();
    }
    if (ImGui::TreeNode("Stage Internals")) {
        pipeline->renderStageInternalsGUI();
        ImGui::TreePop();
    }
}

void RenderRuntime::renderRenderingInternalsGUI()
{
    if (ImGui::TreeNode("Rendering Internals")) {
        if (auto* pipeline = getActivePipelineDebugUI()) {
            drawSettingsSection("Stage Internals", [&]() {
                pipeline->renderStageInternalsGUI();
            });
        }

        ImGui::TreePop();
    }
}

void RenderRuntime::renderGUI(float dt)
{
    (void)dt;

    if (ImGui::TreeNode("Render Targets")) {
        renderRenderTargetEditor();

        if (ImGui::TreeNode("Final Render Target")) {
            if (auto presentationImage = getCurrentPresentationImageShared()) {
                const auto extent = presentationImage->getExtent();
                ImGui::Text("Extent: %u x %u", extent.width, extent.height);
                ImGui::Text("Format: %d", static_cast<int>(presentationImage->getFormat()));
                ImGui::Text("Swapchain images: %u", _render && _render->getSwapchain() ? _render->getSwapchain()->getImageCount() : 0);
                ImGui::Text("Current image index: %u", _render && _render->getSwapchain() ? _render->getSwapchain()->getCurImageIndex() : 0);
                ImGui::Text("Image: %p", presentationImage->getImage() ? presentationImage->getImage()->getHandle().as<void*>() : nullptr);
                ImGui::Text("View: %p", presentationImage->getImageView() ? presentationImage->getImageView()->getHandle().as<void*>() : nullptr);
            }
            ImGui::TreePop();
        }

        ImGui::TreePop();
    }

    if (ImGui::TreeNode("Viewport")) {
        ImGui::DragFloat("Scale", &_viewportFrameBufferScale, 0.1f, 1.0f, 10.0f);
        ImGui::TreePop();
    }

    if (ImGui::TreeNode("Diagnostics")) {
        _diagnostics.renderGUI();
        ImGui::TreePop();
    }
}

void RenderRuntime::renderRenderTargetEditor()
{
    if (!ImGui::TreeNode("RT Editor")) {
        return;
    }

    auto catalog = buildRenderTargetEditorCatalog();
    auto& entries = catalog.entries;

    if (entries.empty()) {
        ImGui::TextUnformatted("No render targets are available.");
        ImGui::TreePop();
        return;
    }

    ImGui::SetNextItemWidth(200.0f);
    ImGui::InputTextWithHint("##runtime-rt-editor-search", "Search RT...", _rtEditor.targetSearch, sizeof(_rtEditor.targetSearch));

    std::vector<int> filteredIndices;
    for (int index = 0; index < static_cast<int>(entries.size()); ++index) {
        if (containsInsensitive(entries[index].label, _rtEditor.targetSearch)) {
            filteredIndices.push_back(index);
        }
    }

    if (filteredIndices.empty()) {
        ImGui::TextUnformatted("No render target matches the current search.");
        ImGui::TreePop();
        return;
    }

    if (_rtEditor.selectedTargetIndex < 0 || _rtEditor.selectedTargetIndex >= static_cast<int>(entries.size()) || std::find(filteredIndices.begin(), filteredIndices.end(), _rtEditor.selectedTargetIndex) == filteredIndices.end()) {
        auto preferredIndex = filteredIndices.front();
        for (int index : filteredIndices) {
            if (!isEntryInitialized(entries[index]) || isSwapChainEntry(entries[index])) {
                continue;
            }
            preferredIndex = index;
            break;
        }

        _rtEditor.selectedTargetIndex     = preferredIndex;
        _rtEditor.selectedAttachmentIndex = 0;
    }

    const auto& selectedEntry = entries[_rtEditor.selectedTargetIndex];
    if (ImGui::BeginCombo("Target", selectedEntry.label)) {
        for (int index : filteredIndices) {
            const bool bSelected = index == _rtEditor.selectedTargetIndex;
            if (ImGui::Selectable(entries[index].label, bSelected)) {
                _rtEditor.selectedTargetIndex     = index;
                _rtEditor.selectedAttachmentIndex = 0;
            }
            if (bSelected) {
                ImGui::SetItemDefaultFocus();
            }
        }
        ImGui::EndCombo();
    }

    if (!isEntryInitialized(selectedEntry)) {
        ImGui::TextUnformatted("Selected RT is not initialized.");
        ImGui::TreePop();
        return;
    }

    const int  colorCount             = getEntryColorAttachmentCount(selectedEntry);
    const bool bHasDepth              = entryHasDepthAttachment(selectedEntry);
    const int  attachmentCount        = colorCount + (bHasDepth ? 1 : 0);
    _rtEditor.selectedAttachmentIndex = std::clamp(_rtEditor.selectedAttachmentIndex, 0, std::max(attachmentCount - 1, 0));

    const std::string selectedAttachmentLabel = _rtEditor.selectedAttachmentIndex < colorCount
                                                  ? std::format("Color[{}]", _rtEditor.selectedAttachmentIndex)
                                                  : std::string("Depth");
    if (attachmentCount > 0 && ImGui::BeginCombo("Attachment", selectedAttachmentLabel.c_str())) {
        for (int attachmentIndex = 0; attachmentIndex < attachmentCount; ++attachmentIndex) {
            const bool        bSelected = attachmentIndex == _rtEditor.selectedAttachmentIndex;
            const std::string label     = attachmentIndex < colorCount ? std::format("Color[{}]", attachmentIndex) : "Depth";
            if (ImGui::Selectable(label.c_str(), bSelected)) {
                _rtEditor.selectedAttachmentIndex = attachmentIndex;
            }
            if (bSelected) {
                ImGui::SetItemDefaultFocus();
            }
        }
        ImGui::EndCombo();
    }

    const Extent2D selectedExtent = getEntryExtent(selectedEntry);
    ImGui::Text("Extent: %u x %u", selectedExtent.width, selectedExtent.height);
    ImGui::Text("Frame Buffers: %u", getEntryFrameBufferCount(selectedEntry));

    EFormat::T currentFormat = attachmentCount > 0
        ? getEntryAttachmentFormat(selectedEntry, _rtEditor.selectedAttachmentIndex)
        : EFormat::Undefined;
    ImGui::Text("Format: %s", formatLabel(currentFormat));

    auto*      sampler     = TextureLibrary::get().getDefaultSampler().get();
    const bool bCanPreview = !isSwapChainEntry(selectedEntry);
    if (!bCanPreview) {
        ImGui::TextWrapped("Preview is disabled for the presentation target because this UI is rendered into the same swapchain image during the screen pass.");
    }
    else if (auto* imageView = getAttachmentImageView(selectedEntry, _rtEditor.selectedAttachmentIndex)) {
        ImGuiHelper::Image(imageView, sampler, "RT Preview", ImVec2(256.0f, 256.0f));
    }

    auto*      pipeline      = getActivePipelineSettingsUI();
    const bool bEditingDepth = _rtEditor.selectedAttachmentIndex >= colorCount && bHasDepth;
    if (!selectedEntry.bEditable) {
        ImGui::SeparatorText("Attachment Format");
        ImGui::TextWrapped("Presentation target format is owned by the swapchain and is currently read-only here.");
        ImGui::TreePop();
        return;
    }
    if (!pipeline) {
        ImGui::SeparatorText("Attachment Format");
        ImGui::TextWrapped("Attachment format editing is currently owned by the active render pipeline and is unavailable because no pipeline settings UI is active.");
        ImGui::TreePop();
        return;
    }

    ImGui::SeparatorText("Attachment Format");
    ImGui::SetNextItemWidth(200.0f);
    ImGui::InputTextWithHint("##runtime-rt-format-search", "Search format...", _rtEditor.formatSearch, sizeof(_rtEditor.formatSearch));

    const char* comboLabel   = bEditingDepth ? "Depth Format" : "Color Format";
    const char* currentLabel = formatLabel(currentFormat);
    if (ImGui::BeginCombo(comboLabel, currentLabel)) {
        if (bEditingDepth) {
            for (const auto& option : kShadowDepthFormats) {
                if (!containsInsensitive(option.label, _rtEditor.formatSearch)) {
                    continue;
                }

                const bool bSelected   = option.format == currentFormat;
                const auto optionLabel = std::string(option.label);
                if (ImGui::Selectable(optionLabel.c_str(), bSelected)) {
                    pipeline->setRenderTargetDepthFormat(selectedEntry.owner, option.format);
                }
                if (bSelected) {
                    ImGui::SetItemDefaultFocus();
                }
            }
        }
        else {
            for (const auto& option : kColorFormats) {
                if (!containsInsensitive(option.label, _rtEditor.formatSearch)) {
                    continue;
                }

                const bool bSelected   = option.format == currentFormat;
                const auto optionLabel = std::string(option.label);
                if (ImGui::Selectable(optionLabel.c_str(), bSelected)) {
                    pipeline->setRenderTargetColorFormat(
                        selectedEntry.owner,
                        static_cast<uint32_t>(_rtEditor.selectedAttachmentIndex),
                        option.format);
                }
                if (bSelected) {
                    ImGui::SetItemDefaultFocus();
                }
            }
        }
        ImGui::EndCombo();
    }

    if (bEditingDepth && (selectedEntry.owner == RenderTargetEditorCatalog::Entry::EOwner::DeferredGBuffer || selectedEntry.owner == RenderTargetEditorCatalog::Entry::EOwner::DeferredViewport)) {
        ImGui::TextWrapped("Deferred GBuffer depth and Deferred Viewport depth are applied together so the current depth copy path stays format-compatible on the next frame.");
    }
    else if (bEditingDepth && (selectedEntry.owner == RenderTargetEditorCatalog::Entry::EOwner::DeferredShadow || selectedEntry.owner == RenderTargetEditorCatalog::Entry::EOwner::ForwardShadow)) {
        ImGui::TextWrapped("D16_UNORM usually reduces depth bandwidth and memory versus D24/D32, but actual gain depends on the GPU and driver. Test with shadow budgets on the target hardware.");
    }

    ImGui::TreePop();
}

} // namespace ya
