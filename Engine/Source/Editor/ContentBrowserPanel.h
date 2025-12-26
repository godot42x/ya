#pragma once

#include "Render/Core/Texture.h"
#include <filesystem>
#include <memory>

namespace ya
{

struct EditorLayer;
struct ImGuiImageEntry;

struct ContentBrowserPanel
{
    EditorLayer          *_owner = nullptr;
    std::filesystem::path _currentDirectory;
    std::filesystem::path _baseDirectory;

    const ImGuiImageEntry *folderIcon = nullptr;
    const ImGuiImageEntry *fileIcon   = nullptr;


  public:
    ContentBrowserPanel(EditorLayer *owner);
    ~ContentBrowserPanel() = default;

    // Deleted copy/move operations
    ContentBrowserPanel(const ContentBrowserPanel &)            = delete;
    ContentBrowserPanel &operator=(const ContentBrowserPanel &) = delete;
    ContentBrowserPanel(ContentBrowserPanel &&)                 = delete;
    ContentBrowserPanel &operator=(ContentBrowserPanel &&)      = delete;

    void init(); // Load resources after EditorLayer is fully initialized
    void onImGuiRender();

  private:
    void renderDirectoryContents();
};

} // namespace ya