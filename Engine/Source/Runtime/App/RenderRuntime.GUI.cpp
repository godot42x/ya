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

bool isDepthAttachmentSelection(const IRenderTarget* rt, int attachmentIndex)
{
    if (!rt) {
        return false;
    }

    const int colorCount = static_cast<int>(rt->getColorAttachmentDescs().size());
    return attachmentIndex >= colorCount && rt->getDepthAttachmentDesc().has_value();
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

IImageView* getAttachmentImageView(IRenderTarget* rt, int attachmentIndex)
{
    if (!rt) {
        return nullptr;
    }

    const int colorCount = static_cast<int>(rt->getColorAttachmentDescs().size());
    if (attachmentIndex < colorCount) {
        auto* colorTexture = rt->getCurrentColorTexture(static_cast<uint32_t>(attachmentIndex));
        return colorTexture ? colorTexture->getImageView() : nullptr;
    }

    if (!rt->getDepthAttachmentDesc().has_value()) {
        return nullptr;
    }

    auto* depthTexture = rt->getCurrentDepthTexture();
    return depthTexture ? depthTexture->getImageView() : nullptr;
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
            if (_screenRT) {
                _screenRT->onRenderGUI();
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
            if (!entries[index].rt || entries[index].rt->isSwapChainTarget()) {
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

    if (!selectedEntry.rt) {
        ImGui::TextUnformatted("Selected RT is not initialized.");
        ImGui::TreePop();
        return;
    }

    const int  colorCount             = static_cast<int>(selectedEntry.rt->getColorAttachmentDescs().size());
    const bool bHasDepth              = selectedEntry.rt->getDepthAttachmentDesc().has_value();
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

    ImGui::Text("Extent: %u x %u", selectedEntry.rt->getExtent().width, selectedEntry.rt->getExtent().height);
    ImGui::Text("Frame Buffers: %u", selectedEntry.rt->getFrameBufferCount());

    EFormat::T currentFormat = EFormat::Undefined;
    if (_rtEditor.selectedAttachmentIndex < colorCount) {
        currentFormat = selectedEntry.rt->getColorAttachmentDescs()[_rtEditor.selectedAttachmentIndex].format;
    }
    else if (bHasDepth) {
        currentFormat = selectedEntry.rt->getDepthAttachmentDesc()->format;
    }
    ImGui::Text("Format: %s", formatLabel(currentFormat));

    auto*      sampler     = TextureLibrary::get().getDefaultSampler().get();
    const bool bCanPreview = !selectedEntry.rt->isSwapChainTarget();
    if (!bCanPreview) {
        ImGui::TextWrapped("Preview is disabled for the presentation target because this UI is rendered into the same swapchain image during the screen pass.");
    }
    else if (auto* imageView = getAttachmentImageView(selectedEntry.rt, _rtEditor.selectedAttachmentIndex)) {
        ImGuiHelper::Image(imageView, sampler, "RT Preview", ImVec2(256.0f, 256.0f));
    }

    const bool bEditingDepth = isDepthAttachmentSelection(selectedEntry.rt, _rtEditor.selectedAttachmentIndex);
    if (!selectedEntry.bEditable) {
        ImGui::SeparatorText("Attachment Format");
        ImGui::TextWrapped("Presentation target format is owned by the swapchain and is currently read-only here.");
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
                    switch (selectedEntry.owner) {
                    case RenderTargetEditorCatalog::Entry::EOwner::DeferredGBuffer:
                    case RenderTargetEditorCatalog::Entry::EOwner::DeferredViewport:
                        setDeferredSharedDepthFormat(option.format);
                        break;
                    default:
                        selectedEntry.rt->setDepthAttachmentFormat(option.format);
                        break;
                    }
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
                    selectedEntry.rt->setColorAttachmentFormat(static_cast<uint32_t>(_rtEditor.selectedAttachmentIndex), option.format);
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
