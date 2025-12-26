#include "ContentBrowserPanel.h"
#include "Core/System/FileSystem.h"
// #include <filesystem>
#include "Core/AssetManager.h"
#include "ImGuiHelper.h"
#include "Render/TextureLibrary.h"
#include "vulkan/vulkan_core.h"
#include <imgui.h>

#include "Core/App/App.h"


namespace ya
{


ContentBrowserPanel::ContentBrowserPanel(EditorLayer *owner)
    : _owner(owner),
      _currentDirectory(std::filesystem::current_path())
{
    _baseDirectory    = FileSystem::get()->getEngineRoot() / "Content";
    _currentDirectory = _baseDirectory;
}

void ContentBrowserPanel::init()
{
    auto am            = AssetManager::get();
    auto fileTexture   = am->loadTexture("file", "Engine/Content/TestTextures/editor/file.png").get();
    auto folderTexture = am->loadTexture("folder", "Engine/Content/TestTextures/editor/folder2.png").get();
    auto sampler       = TextureLibrary::getDefaultSampler();

    fileIcon   = _owner->getOrCreateImGuiTextureID(fileTexture->getImageView(), sampler);
    folderIcon = _owner->getOrCreateImGuiTextureID(folderTexture->getImageView(), sampler);
}

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

            auto icon = entry.is_directory() ? folderIcon : fileIcon;

            ImGui::ImageButton(entry.path().filename().string().c_str(),
                               *icon,
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