#pragma once

#include "Render/Core/Texture.h"
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace ya
{

struct EditorLayer;
struct ImGuiImageEntry;

enum class ContentRootType
{
    Engine,
    Project,
    Plugin
};

struct ContentRoot
{
    std::string           name; // "Engine", "ProjectName", "PluginName"
    std::filesystem::path path; // Physical path to Content folder
    ContentRootType       type;
    bool                  isActive; // Currently selected root
};

struct ContentBrowserPanel
{
    EditorLayer          *_owner = nullptr;
    std::filesystem::path _currentDirectory;

    std::vector<ContentRoot> _contentRoots;         // All available content roots
    ContentRoot             *_activeRoot = nullptr; // Currently browsing root

    float _leftPanelWidth = 200.0f; // Adjustable left panel width

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
    void discoverContentRoots();
    void switchToRoot(ContentRoot *root);
    bool isPathWithinActiveRoot(const std::filesystem::path &path) const;
    void renderRootSelector();
    void renderDirectoryContents();
};

} // namespace ya