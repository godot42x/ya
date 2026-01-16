#pragma once

#include "FileExplorer.h"
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace ya
{

struct EditorLayer;
struct ImGuiImageEntry;

/**
 * @brief ContentBrowser 面板，使用 FileExplorer 组件
 */
struct ContentBrowserPanel
{
    EditorLayer          *_owner = nullptr;

    // FileExplorer handles all browsing logic
    FileExplorer _fileExplorer;

    // Icons for file/folder display
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
};

} // namespace ya