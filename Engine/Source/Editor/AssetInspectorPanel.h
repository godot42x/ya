#pragma once

#include "Resource/AssetMeta.h"
#include "Render/Core/Texture.h"

#include <array>
#include <string>

namespace ya
{

struct EditorLayer;
struct ImGuiImageEntry;

struct AssetInspectorPanel
{
  private:
    EditorLayer* _owner = nullptr;

    std::string _inspectedPath;
    AssetMeta   _meta;
    bool        _bVisible = false;
    bool        _bDirty   = false; // meta was edited, needs save
    std::shared_ptr<Texture> _previewTexture;
    bool                     _bPreviewRequested = false;
    std::array<bool, 4>      _previewChannelEnabled = {true, true, true, true};
    std::shared_ptr<IImageView> _previewMaskedView;
    IImageView*                 _previewLastBase = nullptr;

    // Preview
    const ImGuiImageEntry* _previewImGuiID = nullptr;

  public:
    AssetInspectorPanel(EditorLayer* owner);

    AssetInspectorPanel(const AssetInspectorPanel&)            = delete;
    AssetInspectorPanel& operator=(const AssetInspectorPanel&) = delete;
    AssetInspectorPanel(AssetInspectorPanel&&)                 = delete;
    AssetInspectorPanel& operator=(AssetInspectorPanel&&)      = delete;

    /// Open inspector for a texture asset
    void inspectTexture(const std::string& relativePath);

    /// Close / clear
    void clear();

    /// Render the ImGui panel
    void onImGuiRender();

    bool isVisible() const { return _bVisible; }

  private:
    void renderTextureInspector();
    bool renderPreviewMaskControls();
    void updatePreviewMaskView(bool bForceRefresh = false);
    void resetPreviewState();

    /// Save current meta to disk and trigger hot-reload
    void applyMetaChanges();
};

} // namespace ya
