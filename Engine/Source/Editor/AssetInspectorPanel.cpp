#include "AssetInspectorPanel.h"

#include "EditorLayer.h"
#include "Resource/AssetManager.h"
#include "Resource/AssetMeta.h"
#include "Resource/TextureLibrary.h"
#include "Runtime/App/App.h"

#include <imgui.h>
#include <imgui_internal.h>

namespace ya
{

static const char* kWindowTitle = "Asset Inspector";

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

    // ── Color Space ─────────────────────────────────────────────────────
    static const char* colorSpaceLabels[] = {"sRGB", "Linear"};
    std::string csStr   = _meta.getString("colorSpace", "srgb");
    int         csIndex = (csStr == "linear") ? 1 : 0;

    if (ImGui::Combo("Color Space", &csIndex, colorSpaceLabels, IM_ARRAYSIZE(colorSpaceLabels))) {
        _meta.properties["colorSpace"] = (csIndex == 1) ? "linear" : "srgb";
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
