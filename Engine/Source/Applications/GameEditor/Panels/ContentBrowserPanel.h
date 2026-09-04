#pragma once

namespace ya
{

struct EditorLayer;
struct ImGuiImageEntry;

/// Loads shared file/folder icons for editor chrome widgets that still use
/// ImGui texture descriptors (FilePicker). Retained Content Browser lives in
/// EditorSurface; the legacy ImGui panel was removed in Phase 8A.
struct ContentBrowserPanel
{
    EditorLayer* _owner = nullptr;

    const ImGuiImageEntry* folderIcon = nullptr;
    const ImGuiImageEntry* fileIcon   = nullptr;

  public:
    explicit ContentBrowserPanel(EditorLayer* owner);

    ContentBrowserPanel(const ContentBrowserPanel&)            = delete;
    ContentBrowserPanel& operator=(const ContentBrowserPanel&) = delete;
    ContentBrowserPanel(ContentBrowserPanel&&)                 = delete;
    ContentBrowserPanel& operator=(ContentBrowserPanel&&)      = delete;

    void init();
};

} // namespace ya
