#include "RenderRuntime.h"

#include "Core/Debug/RenderDocCapture.h"
#include "DeferredRender/DeferredRenderPipeline.h"
#include "ImGuiHelper.h"
#include "Resource/TextureLibrary.h"
#include "Runtime/App/ForwardRender/ForwardRenderPipeline.h"

#include <SDL3/SDL.h>
#include <algorithm>
#include <array>
#include <cctype>
#include <filesystem>
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

struct RuntimeRenderTargetEditorEntry
{
    enum class EOwner
    {
        Presentation,
        ForwardViewport,
        ForwardShadow,
        DeferredGBuffer,
        DeferredViewport,
        DeferredShadow,
    };

    const char*    label     = "";
    IRenderTarget* rt        = nullptr;
    EOwner         owner     = EOwner::Presentation;
    bool           bEditable = true;
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

    auto* frameBuffer = rt->getCurFrameBuffer();
    if (!frameBuffer) {
        return nullptr;
    }

    const int colorCount = static_cast<int>(rt->getColorAttachmentDescs().size());
    if (attachmentIndex < colorCount) {
        auto* colorTexture = frameBuffer->getColorTexture(static_cast<uint32_t>(attachmentIndex));
        return colorTexture ? colorTexture->getImageView() : nullptr;
    }

    if (!rt->getDepthAttachmentDesc().has_value()) {
        return nullptr;
    }

    auto* depthTexture = frameBuffer->getDepthTexture();
    return depthTexture ? depthTexture->getImageView() : nullptr;
}

void openCaptureDirectoryFromGUI(const std::string& filePath)
{
    if (filePath.empty()) {
        YA_CORE_WARN("File path is empty, cannot open directory");
        return;
    }

    auto dir = std::filesystem::path(filePath).parent_path();
    if (dir.empty()) {
        YA_CORE_WARN("Directory path is empty for file: {}", filePath);
        return;
    }

    dir = std::filesystem::absolute(dir);
    const auto url = std::format("file:///{}", dir.string());
    if (!SDL_OpenURL(url.c_str())) {
        YA_CORE_ERROR("Failed to open directory {}: {}", dir.string(), SDL_GetError());
    }
}

} // namespace

void RenderRuntime::renderGUI(float dt)
{
    (void)dt;

    if (ImGui::TreeNode("World Rendering")) {
        static const char* items[] = {"Forward", "Deferred"};
        int                current = static_cast<int>(_pendingShadingModel);
        if (ImGui::Combo("Shading Model", &current, items, IM_ARRAYSIZE(items))) {
            _pendingShadingModel = static_cast<EShadingModel>(current);
        }
        if (_pendingShadingModel != _shadingModel) {
            ImGui::SameLine();
            ImGui::TextColored(ImVec4(1, 1, 0, 1), "(switch pending)");
        }
        if (_forwardPipeline) {
            _forwardPipeline->renderGUI();
        }
        if (_deferredPipeline) {
            _deferredPipeline->renderGUI();
        }
        ImGui::TreePop();
    }

    renderRenderTargetEditor();

    if (ImGui::TreeNode("Final Render Target")) {
        if (_screenRT) {
            _screenRT->onRenderGUI();
        }
        ImGui::TreePop();
    }

    ImGui::DragFloat("Viewport Scale", &_viewportFrameBufferScale, 0.1f, 1.0f, 10.0f);

    if (ImGui::TreeNode("RenderDoc")) {
        bool bAvailable = _renderDoc.capture && _renderDoc.capture->isAvailable();
        ImGui::Text("Available: %s", bAvailable ? "Yes" : "No");
        ImGui::TextWrapped("DLL Path: %s", _renderDoc.configuredDllPath.empty() ? "<default>" : _renderDoc.configuredDllPath.c_str());
        ImGui::TextWrapped("Output Dir: %s", _renderDoc.configuredOutputDir.empty() ? "<default>" : _renderDoc.configuredOutputDir.c_str());
        if (bAvailable) {
            bool bCaptureEnabled = _renderDoc.capture->isCaptureEnabled();
            if (ImGui::Checkbox("Capture Enabled", &bCaptureEnabled)) {
                _renderDoc.capture->setCaptureEnabled(bCaptureEnabled);
            }

            bool bHudVisible = _renderDoc.capture->isHUDVisible();
            if (ImGui::Checkbox("Show RenderDoc HUD", &bHudVisible)) {
                _renderDoc.capture->setHUDVisible(bHudVisible);
            }

            ImGui::Text("Capturing: %s", _renderDoc.capture->isCapturing() ? "Yes" : "No");
            ImGui::Text("Delay Frames: %u", _renderDoc.capture->getDelayFrames());
            ImGui::Combo("On Capture", &_renderDoc.onCaptureAction, "None\0Open Replay UI\0Open Capture Folder\0");
            ImGui::TextWrapped("Last Capture: %s", _renderDoc.lastCapturePath.empty() ? "<none>" : _renderDoc.lastCapturePath.c_str());

            bool bCanCapture = _renderDoc.capture->isCaptureEnabled();
            ImGui::BeginDisabled(!bCanCapture);
            if (ImGui::Button("Capture Next Frame (F9)")) {
                _renderDoc.capture->requestNextFrame();
            }
            if (ImGui::Button("Capture After 120 Frames (Ctrl+F9)")) {
                _renderDoc.capture->requestAfterFrames(120);
            }
            ImGui::EndDisabled();

            if (ImGui::Button("Open Last Capture Folder") && !_renderDoc.lastCapturePath.empty()) {
                openCaptureDirectoryFromGUI(_renderDoc.lastCapturePath);
            }
        }
        ImGui::TreePop();
    }
}

void RenderRuntime::renderRenderTargetEditor()
{
    if (!ImGui::TreeNode("RT Editor")) {
        return;
    }

    std::vector<RuntimeRenderTargetEditorEntry> entries;
    if (_screenRT) {
        entries.push_back({.label = "Presentation", .rt = _screenRT.get(), .owner = RuntimeRenderTargetEditorEntry::EOwner::Presentation, .bEditable = false});
    }
    if (_forwardPipeline) {
        entries.push_back({.label = "Forward Viewport", .rt = _forwardPipeline->getViewportRT(), .owner = RuntimeRenderTargetEditorEntry::EOwner::ForwardViewport});
        entries.push_back({.label = "Forward Shadow", .rt = _forwardPipeline->getShadowDepthRT(), .owner = RuntimeRenderTargetEditorEntry::EOwner::ForwardShadow});
    }
    if (_deferredPipeline) {
        entries.push_back({.label = "Deferred GBuffer", .rt = _deferredPipeline->getGBufferRT(), .owner = RuntimeRenderTargetEditorEntry::EOwner::DeferredGBuffer});
        entries.push_back({.label = "Deferred Viewport", .rt = _deferredPipeline->getViewportRT(), .owner = RuntimeRenderTargetEditorEntry::EOwner::DeferredViewport});
        entries.push_back({.label = "Deferred Shadow", .rt = _deferredPipeline->getShadowDepthRT(), .owner = RuntimeRenderTargetEditorEntry::EOwner::DeferredShadow});
    }

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

    const int  colorCount            = static_cast<int>(selectedEntry.rt->getColorAttachmentDescs().size());
    const bool bHasDepth             = selectedEntry.rt->getDepthAttachmentDesc().has_value();
    const int  attachmentCount       = colorCount + (bHasDepth ? 1 : 0);
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

    auto* sampler = TextureLibrary::get().getDefaultSampler().get();
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

                const bool bSelected = option.format == currentFormat;
                const auto optionLabel = std::string(option.label);
                if (ImGui::Selectable(optionLabel.c_str(), bSelected)) {
                    switch (selectedEntry.owner) {
                    case RuntimeRenderTargetEditorEntry::EOwner::DeferredGBuffer:
                    case RuntimeRenderTargetEditorEntry::EOwner::DeferredViewport:
                        if (_deferredPipeline) {
                            if (auto* gbufferRT = _deferredPipeline->getGBufferRT()) {
                                gbufferRT->setDepthAttachmentFormat(option.format);
                            }
                            if (auto* viewportRT = _deferredPipeline->getViewportRT()) {
                                viewportRT->setDepthAttachmentFormat(option.format);
                            }
                        }
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

                const bool bSelected = option.format == currentFormat;
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

    if (bEditingDepth && (selectedEntry.owner == RuntimeRenderTargetEditorEntry::EOwner::DeferredGBuffer || selectedEntry.owner == RuntimeRenderTargetEditorEntry::EOwner::DeferredViewport)) {
        ImGui::TextWrapped("Deferred GBuffer depth and Deferred Viewport depth are applied together so the current depth copy path stays format-compatible on the next frame.");
    }
    else if (bEditingDepth && (selectedEntry.owner == RuntimeRenderTargetEditorEntry::EOwner::DeferredShadow || selectedEntry.owner == RuntimeRenderTargetEditorEntry::EOwner::ForwardShadow)) {
        ImGui::TextWrapped("D16_UNORM usually reduces depth bandwidth and memory versus D24/D32, but actual gain depends on the GPU and driver. Test with shadow budgets on the target hardware.");
    }

    ImGui::TreePop();
}

} // namespace ya