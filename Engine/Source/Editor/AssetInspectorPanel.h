#pragma once

#include "Resource/AssetMeta.h"
#include "Render/Core/Texture.h"
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

    /// Save current meta to disk and trigger hot-reload
    void applyMetaChanges();
};

} // namespace ya
