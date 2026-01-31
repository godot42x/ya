#include "ContentBrowserPanel.h"
#include "Core/App/App.h"
#include "Core/AssetManager.h"
#include "Core/Debug/Instrumentor.h"
#include "Core/System/VirtualFileSystem.h"
#include "ImGuiHelper.h"
#include "Resource/TextureLibrary.h"
#include <imgui.h>

namespace ya
{

ContentBrowserPanel::ContentBrowserPanel(EditorLayer *owner)
    : _owner(owner)
{
}

void ContentBrowserPanel::init()
{
    // Load icons
    auto am            = AssetManager::get();
    auto fileTexture   = am->loadTexture("file", "Engine/Content/TestTextures/editor/file.png").get();
    auto folderTexture = am->loadTexture("folder", "Engine/Content/TestTextures/editor/folder2.png").get();
    auto sampler       = TextureLibrary::get().getDefaultSampler();

    fileIcon   = _owner->getOrCreateImGuiTextureID(fileTexture->getImageView(), sampler);
    folderIcon = _owner->getOrCreateImGuiTextureID(folderTexture->getImageView(), sampler);

    // Initialize FileExplorer from VFS
    _fileExplorer.initFromVFS();
    _fileExplorer.setViewMode(FileExplorer::ViewMode::Icon);
    _fileExplorer.setFilterMode(FileExplorer::FilterMode::Both);
    _fileExplorer.setSelectionMode(FileExplorer::SelectionMode::File);
    _fileExplorer.setLeftPanelWidth(200.0f);
    _fileExplorer.setIcons({.folder = folderIcon, .file = fileIcon});
    _fileExplorer.setThumbnailSize(94.0f);
    _fileExplorer.setPadding(16.0f);
    _fileExplorer.setShowViewModeToggle(true);
    _fileExplorer.setShowSizeSlider(true);

    // Set item action callback for opening scene files
    _fileExplorer.setItemActionCallback([](const std::filesystem::path &path) {
        if (path.string().ends_with(".scene.json"))
        {
            App::get()->taskManager.registerFrameTask(
                [scenePath = path.string()]() {
                    App::get()->loadScene(scenePath);
                });
        }
    });
}

void ContentBrowserPanel::onImGuiRender()
{
    YA_PROFILE_FUNCTION();

    if (!ImGui::Begin("Content Browser"))
    {
        ImGui::End();
        return;
    }

    // FileExplorer handles everything: mount point selector, directory contents, view modes
    _fileExplorer.render(nullptr, -1);

    ImGui::End();
}

} // namespace ya