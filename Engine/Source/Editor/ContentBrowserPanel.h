#pragma once

#include <filesystem>

namespace ya {

class ContentBrowserPanel
{
    std::filesystem::path _currentDirectory;

  public:
    ContentBrowserPanel();
    ~ContentBrowserPanel() = default;

    // Deleted copy/move operations
    ContentBrowserPanel(const ContentBrowserPanel&) = delete;
    ContentBrowserPanel& operator=(const ContentBrowserPanel&) = delete;
    ContentBrowserPanel(ContentBrowserPanel&&) = delete;
    ContentBrowserPanel& operator=(ContentBrowserPanel&&) = delete;

    void onImGuiRender();

  private:
    void renderDirectoryContents();
};

} // namespace ya
