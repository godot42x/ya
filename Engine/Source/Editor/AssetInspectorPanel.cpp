#include "AssetInspectorPanel.h"

#include "EditorLayer.h"
#include "Resource/AssetManager.h"
#include "Resource/AssetMeta.h"
#include "Resource/TextureLibrary.h"
#include "Runtime/App/App.h"

#include <algorithm>
#include <imgui.h>
#include <imgui_internal.h>
#include <string_view>

namespace ya
{

static const char* kWindowTitle = "Asset Inspector";

namespace
{
int comboIndexForValue(const char* const* labels, int count, std::string_view value, int defaultIndex = 0)
{
    for (int i = 0; i < count; ++i) {
        if (value == labels[i]) {
            return i;
        }
    }
    return defaultIndex;
}
}

AssetInspectorPanel::AssetInspectorPanel(EditorLayer* owner)
    : _owner(owner)
{
}

void AssetInspectorPanel::inspectTexture(const std::string& relativePath)
{
    if (relativePath == _inspectedPath && _bVisible) {
        // Already inspecting this asset — just focus the window
        if (auto* window = ImGui::FindWindowByName(kWindowTitle)) {
            ImGui::FocusWindow(window);
        }
        return;
    }

    _inspectedPath = relativePath;
    _meta          = AssetManager::get()->getOrLoadMeta(relativePath);
    _bVisible      = true;
    _bDirty        = false;
    _previewImGuiID = nullptr; // Will be refreshed on next render

    if (auto* window = ImGui::FindWindowByName(kWindowTitle)) {
        ImGui::FocusWindow(window);
    }
}

void AssetInspectorPanel::clear()
{
    _inspectedPath.clear();
    _meta    = {};
    _bDirty  = false;
    _previewImGuiID = nullptr;
}

void AssetInspectorPanel::onImGuiRender()
{
    if (!_bVisible) return;

    if (!ImGui::Begin(kWindowTitle, &_bVisible)) {
        ImGui::End();
        return;
    }

    if (_inspectedPath.empty()) {
        ImGui::TextDisabled("No asset selected");
    }
    else {
        renderTextureInspector();
    }

    ImGui::End();
}

void AssetInspectorPanel::renderTextureInspector()
{
    // ── Path ────────────────────────────────────────────────────────────
    ImGui::TextWrapped("Path: %s", _inspectedPath.c_str());
    ImGui::Separator();

    // ── Preview ─────────────────────────────────────────────────────────
    auto texFuture = AssetManager::get()->loadTexture(AssetManager::TextureLoadRequest{
        .filepath = _inspectedPath,
    });
    if (texFuture.isReady()) {
        auto* texture = texFuture.get();
        if (texture && texture->isValid()) {
            // Create/get ImGui texture ID for preview
            auto sampler = TextureLibrary::get().getLinearSampler();
            if (sampler && !_previewImGuiID) {
                _previewImGuiID = _owner->getOrCreateImGuiTextureID(
                    texture->getImageView(), sampler);
            }

            if (_previewImGuiID) {
                float maxSize = std::min(ImGui::GetContentRegionAvail().x, 256.0f);
                float w       = static_cast<float>(texture->getWidth());
                float h       = static_cast<float>(texture->getHeight());
                float scale   = (w > 0.f && h > 0.f) ? std::min(maxSize / w, maxSize / h) : 1.0f;
                ImGui::Image(*_previewImGuiID, ImVec2(w * scale, h * scale));
            }

            ImGui::Text("Dimensions: %ux%u", texture->getWidth(), texture->getHeight());
            ImGui::Text("Format: %d", (int)texture->getFormat());
            ImGui::Text("Channels: %u", texture->getChannels());
        }
        else {
            ImGui::TextDisabled("Texture invalid or not yet loaded");
        }
    }
    else {
        ImGui::TextDisabled("Loading...");
    }

    ImGui::Separator();

    auto importSettings = AssetManager::get()->resolveTextureImportSettings(_inspectedPath, AssetManager::ETextureColorSpace::SRGB);
    ImGui::Text("Detected: %s", AssetManager::textureSourceKindName(importSettings.sourceInfo.detectedKind));
    ImGui::Text("Resolved Format: %d", static_cast<int>(importSettings.resolvedFormat));
    ImGui::Text("Payload: %s", AssetManager::texturePayloadTypeName(importSettings.payloadType));
    ImGui::Text("Detected Channels: %u", importSettings.sourceInfo.detectedChannels);
    ImGui::Separator();

    // ── Color Space ─────────────────────────────────────────────────────
    static const char* colorSpaceLabels[] = {"srgb", "linear"};
    std::string csStr   = _meta.getString("colorSpace", "srgb");
    int         csIndex = comboIndexForValue(colorSpaceLabels, IM_ARRAYSIZE(colorSpaceLabels), csStr, 0);

    if (ImGui::Combo("Color Space", &csIndex, colorSpaceLabels, IM_ARRAYSIZE(colorSpaceLabels))) {
        _meta.properties["colorSpace"] = colorSpaceLabels[csIndex];
        _bDirty = true;
    }

    static const char* sourceKindLabels[] = {"auto", "ldr", "hdr", "data", "compressed"};
    std::string sourceKind = _meta.getString("sourceKind", "auto");
    int sourceKindIndex = comboIndexForValue(sourceKindLabels, IM_ARRAYSIZE(sourceKindLabels), sourceKind, 0);
    if (ImGui::Combo("Source Kind", &sourceKindIndex, sourceKindLabels, IM_ARRAYSIZE(sourceKindLabels))) {
        _meta.properties["sourceKind"] = sourceKindLabels[sourceKindIndex];
        _bDirty = true;
    }

    static const char* precisionLabels[] = {"auto", "u8", "f16", "f32"};
    std::string precision = _meta.getString("decodePrecision", "auto");
    int precisionIndex = comboIndexForValue(precisionLabels, IM_ARRAYSIZE(precisionLabels), precision, 0);
    if (ImGui::Combo("Decode Precision", &precisionIndex, precisionLabels, IM_ARRAYSIZE(precisionLabels))) {
        _meta.properties["decodePrecision"] = precisionLabels[precisionIndex];
        _bDirty = true;
    }

    static const char* channelPolicyLabels[] = {"force_rgba", "preserve"};
    std::string channelPolicy = _meta.getString("channelPolicy", "force_rgba");
    int channelPolicyIndex = comboIndexForValue(channelPolicyLabels, IM_ARRAYSIZE(channelPolicyLabels), channelPolicy, 0);
    if (ImGui::Combo("Channel Policy", &channelPolicyIndex, channelPolicyLabels, IM_ARRAYSIZE(channelPolicyLabels))) {
        _meta.properties["channelPolicy"] = channelPolicyLabels[channelPolicyIndex];
        _bDirty = true;
    }

    static const char* uploadFormatLabels[] = {"auto", "r8_unorm", "r8g8_unorm", "r8g8b8a8_unorm", "r8g8b8a8_srgb", "r16g16b16a16_sfloat", "r32_sfloat"};
    std::string uploadFormat = _meta.getString("preferredUploadFormat", "auto");
    int uploadFormatIndex = comboIndexForValue(uploadFormatLabels, IM_ARRAYSIZE(uploadFormatLabels), uploadFormat, 0);
    if (ImGui::Combo("Upload Format", &uploadFormatIndex, uploadFormatLabels, IM_ARRAYSIZE(uploadFormatLabels))) {
        _meta.properties["preferredUploadFormat"] = uploadFormatLabels[uploadFormatIndex];
        _bDirty = true;
    }

    // ── Generate Mips ───────────────────────────────────────────────────
    bool genMips = _meta.getBool("generateMips", true);
    if (ImGui::Checkbox("Generate Mips", &genMips)) {
        _meta.properties["generateMips"] = genMips;
        _bDirty = true;
    }

    // ── Filter ──────────────────────────────────────────────────────────
    static const char* filterLabels[] = {"Linear", "Nearest"};
    std::string filterStr   = _meta.getString("filter", "linear");
    int         filterIndex = (filterStr == "nearest") ? 1 : 0;

    if (ImGui::Combo("Filter", &filterIndex, filterLabels, IM_ARRAYSIZE(filterLabels))) {
        _meta.properties["filter"] = (filterIndex == 1) ? "nearest" : "linear";
        _bDirty = true;
    }

    ImGui::Separator();

    // ── Apply / Revert ──────────────────────────────────────────────────
    ImGui::BeginDisabled(!_bDirty);
    if (ImGui::Button("Apply")) {
        applyMetaChanges();
    }
    ImGui::SameLine();
    if (ImGui::Button("Revert")) {
        _meta   = AssetManager::get()->reloadMeta(_inspectedPath);
        _bDirty = false;
    }
    ImGui::EndDisabled();

    if (_bDirty) {
        ImGui::SameLine();
        ImGui::TextColored(ImVec4(1, 0.8f, 0, 1), "(unsaved changes)");
    }

    // ── Meta file path info ─────────────────────────────────────────────
    ImGui::Separator();
    ImGui::TextDisabled("Meta: %s", AssetMeta::metaPathFor(_inspectedPath).c_str());
}

void AssetInspectorPanel::applyMetaChanges()
{
    // Save meta to disk
    std::string metaPath = AssetMeta::metaPathFor(_inspectedPath);
    _meta.type = "texture";
    _meta.saveToFile(metaPath);

    // Trigger hot-reload through AssetManager
    AssetManager::get()->onMetaFileChanged(metaPath);

    _bDirty         = false;
    _previewImGuiID = nullptr; // Force refresh after reload

    YA_CORE_INFO("AssetInspectorPanel: Applied meta changes for '{}'", _inspectedPath);
}

} // namespace ya
