#include "ContentBrowserPanel.h"
#include "Core/FileSystem/FileSystem.h"
// #include <filesystem>
#include "Core/AssetManager.h"
#include "ImGuiHelper.h"
#include "Render/TextureLibrary.h"
#include "vulkan/vulkan_core.h"
#include <imgui.h>

#include "Core/App/App.h"


namespace ya
{

VkDescriptorSet vkFileIcon;
VkDescriptorSet vkFolderIcon;

ContentBrowserPanel::ContentBrowserPanel()
    : _currentDirectory(std::filesystem::current_path())
{
    _baseDirectory    = FileSystem::get()->getEngineRoot() / "Content";
    _currentDirectory = _baseDirectory;


    App::get()->onScenePostInit.addLambda(
        this,
        [this]() {
            auto am    = AssetManager::get();
            fileIcon   = am->loadTexture("file", "Engine/Content/TestTextures/editor/file.png").get();
            folderIcon = am->loadTexture("folder", "Engine/Content/TestTextures/editor/folder2.png").get();
            // am->loadTexture("pause", "Engine/Content/TestTextures/editor/pause.png");
            // am->loadTexture("play", "Engine/Content/TestTextures/editor/play.png");
            // am->loadTexture("stop", "Engine/Content/TestTextures/editor/stop.png");
            // am->loadTexture("simulate_button", "Engine/Content/TestTextures/editor/simulate_button.png");
            auto sampler = TextureLibrary::getDefaultSampler();

            vkFileIcon   = (VkDescriptorSet)ImGuiManager::get().addTexture(fileIcon->getImageView()->getHandle(), sampler->getHandle());
            vkFolderIcon = (VkDescriptorSet)ImGuiManager::get().addTexture(folderIcon->getImageView()->getHandle(), sampler->getHandle());

            TODO("Implement remove subscription");
            // App::get()->onScenePostInit.remove(this);
        });
};

void ContentBrowserPanel::onImGuiRender()
{
    if (!ImGui::Begin("Content Browser"))
    {
        ImGui::End();
        return;
    }

    if (_currentDirectory != _baseDirectory && !_baseDirectory.empty())
    {
        if (ImGui::Button("< Back"))
        {
            _currentDirectory = _currentDirectory.parent_path();
        }
    }

    ImGui::Text("Current: %s", _currentDirectory.string().c_str());
    ImGui::Separator();

    renderDirectoryContents();

    ImGui::End();
}

void ContentBrowserPanel::renderDirectoryContents()
{
    static float padding        = 16.0f;
    static float thumbnail_size = 94.0f;

    float cellSize = thumbnail_size + padding;

    float panelWidth  = ImGui::GetContentRegionAvail().x;
    int   columnCount = static_cast<int>(panelWidth / cellSize);
    if (columnCount < 1)
        columnCount = 1;

    ImGui::Columns(columnCount, 0, false);
    try
    {
        for (auto &entry : std::filesystem::directory_iterator(_currentDirectory))
        {
            const auto &path     = entry.path();
            auto        filename = path.filename().string();

            ImGui::PushID(filename.c_str());

            auto icon = entry.is_directory() ? vkFolderIcon : vkFileIcon;

            ImGui::ImageButton(
                entry.path().filename().string().c_str(),
                (ImTextureID)(uintptr_t)icon,
                {thumbnail_size, thumbnail_size},
                {0, 0},
                {1, 1});

            if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
            {
                if (entry.is_directory())
                {
                    _currentDirectory = path;
                }
            }
            ImGui::TextWrapped("%s", filename.c_str());


            ImGui::PopID();
            ImGui::NextColumn();
        }
    }
    catch (const std::exception &e)
    {
        ImGui::TextColored(ImVec4(1, 0, 0, 1), "Error reading directory: %s", e.what());
    }

    ImGui::Columns(1);

    ImGui::DragFloat("Thumbnail Size", &thumbnail_size, 0.1f, 16.0f, 256.0f);
    ImGui::DragFloat("Padding", &padding, 0.1f, 0.0f, 64.0f);
}

} // namespace ya