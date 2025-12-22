#pragma once

#include <filesystem>
#include <memory>

namespace ya
{

struct ContentBrowserPanel
{
    std::filesystem::path _currentDirectory;
    std::filesystem::path _baseDirectory;

  public:
    ContentBrowserPanel();
    ~ContentBrowserPanel() = default;

    // Deleted copy/move operations
    ContentBrowserPanel(const ContentBrowserPanel &)            = delete;
    ContentBrowserPanel &operator=(const ContentBrowserPanel &) = delete;
    ContentBrowserPanel(ContentBrowserPanel &&)                 = delete;
    ContentBrowserPanel &operator=(ContentBrowserPanel &&)      = delete;

    void onImGuiRender();

  private:
    void renderDirectoryContents();
};

} // namespace ya