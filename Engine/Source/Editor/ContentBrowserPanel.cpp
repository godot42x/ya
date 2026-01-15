#include "ContentBrowserPanel.h"
#include "Core/App/App.h"
#include "Core/AssetManager.h"
#include "Core/Debug/Instrumentor.h"
#include "Core/System/FileSystem.h"
#include "ImGuiHelper.h"
#include "Render/TextureLibrary.h"
#include "vulkan/vulkan_core.h"
#include <imgui.h>


namespace ya
{

ContentBrowserPanel::ContentBrowserPanel(EditorLayer *owner)
    : _owner(owner)
{
    discoverContentRoots();

    // Default to Engine Content
    if (!_contentRoots.empty())
    {
        switchToRoot(&_contentRoots[0]);
    }
}

void ContentBrowserPanel::discoverContentRoots()
{
    auto fs = FileSystem::get();

    // 1. Engine Content
    auto engineContentPath = fs->getEngineRoot() / "Content";
    if (std::filesystem::exists(engineContentPath))
    {
        _contentRoots.push_back({
            .name     = "Engine",
            .path     = engineContentPath,
            .type     = ContentRootType::Engine,
            .isActive = false,
        });
    }

    // 2. Project/Game Content
    if (!fs->getGameRoot().empty())
    {
        auto gameContentPath = fs->getGameRoot() / "Content";
        if (std::filesystem::exists(gameContentPath))
        {
            auto gameName = fs->getGameRoot().filename().string();
            _contentRoots.push_back({
                .name     = gameName,
                .path     = gameContentPath,
                .type     = ContentRootType::Project,
                .isActive = false,
            });
        }
    }

    // 3. Plugin Contents
    for (const auto &[pluginName, pluginRoot] : fs->getPluginRoots())
    {
        auto pluginContentPath = pluginRoot / "Content";
        if (std::filesystem::exists(pluginContentPath))
        {
            _contentRoots.push_back({
                .name     = pluginName,
                .path     = pluginContentPath,
                .type     = ContentRootType::Plugin,
                .isActive = false,
            });
        }
    }
}

void ContentBrowserPanel::switchToRoot(ContentRoot *root)
{
    if (!root) return;

    // Deactivate previous root
    if (_activeRoot)
    {
        _activeRoot->isActive = false;
    }

    // Activate new root
    _activeRoot           = root;
    _activeRoot->isActive = true;
    _currentDirectory     = _activeRoot->path;
}

bool ContentBrowserPanel::isPathWithinActiveRoot(const std::filesystem::path &path) const
{
    if (!_activeRoot) return false;

    auto relativePath = std::filesystem::relative(path, _activeRoot->path);
    // If relative path starts with "..", it's outside the root
    return !relativePath.empty() && !relativePath.string().starts_with("..");
}

void ContentBrowserPanel::init()
{
    auto am            = AssetManager::get();
    auto fileTexture   = am->loadTexture("file", "Engine/Content/TestTextures/editor/file.png").get();
    auto folderTexture = am->loadTexture("folder", "Engine/Content/TestTextures/editor/folder2.png").get();
    auto sampler       = TextureLibrary::get().getDefaultSampler();

    fileIcon   = _owner->getOrCreateImGuiTextureID(fileTexture->getImageView(), sampler);
    folderIcon = _owner->getOrCreateImGuiTextureID(folderTexture->getImageView(), sampler);
}

void ContentBrowserPanel::onImGuiRender()
{
    YA_PROFILE_FUNCTION();
    if (!ImGui::Begin("Content Browser"))
    {
        ImGui::End();
        return;
    }

    // Split into left sidebar (roots) and right content area

    // Left panel - Content roots list
    ImGui::BeginChild("RootsList", ImVec2(_leftPanelWidth, 0), true);
    renderRootSelector();
    ImGui::EndChild();

    ImGui::SameLine();

    // Splitter
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImGui::GetStyleColorVec4(ImGuiCol_Button));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImGui::GetStyleColorVec4(ImGuiCol_Button));
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(-19, 0));
    ImGui::Button("##splitter", ImVec2(8.0f, -1));
    ImGui::PopStyleColor(2);
    ImGui::PopStyleVar(1);


    if (ImGui::IsItemActive())
    {
        _leftPanelWidth += ImGui::GetIO().MouseDelta.x;
        if (_leftPanelWidth < 100.0f) _leftPanelWidth = 100.0f;
        if (_leftPanelWidth > 400.0f) _leftPanelWidth = 400.0f;
    }
    if (ImGui::IsItemHovered())
    {
        ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
    }

    ImGui::SameLine();

    // Right panel - Directory browser
    ImGui::BeginChild("ContentArea", ImVec2(0, 0), false);

    bool bFocused = ImGui::IsWindowFocused();
    bool bExit    = ImGui::IsMouseClicked(3) && bFocused;


    // Navigation bar
    if (_activeRoot && _currentDirectory != _activeRoot->path)
    {
        if (ImGui::Button("< Back") || bExit)
        {
            auto parent = _currentDirectory.parent_path();
            // Only allow navigation within active root
            if (isPathWithinActiveRoot(parent) || parent == _activeRoot->path)
            {
                _currentDirectory = parent;
            }
        }
        ImGui::SameLine();
    }

    // Show current path relative to root
    if (_activeRoot)
    {
        auto relativePath = std::filesystem::relative(_currentDirectory, _activeRoot->path);
        ImGui::Text("%s: %s", _activeRoot->name.c_str(), relativePath.empty() ? "/" : relativePath.string().c_str());
    }
    else
    {
        ImGui::TextColored(ImVec4(1, 0.5f, 0, 1), "No content root selected");
    }

    ImGui::Separator();

    // Directory contents
    if (_activeRoot)
    {
        renderDirectoryContents();
    }

    ImGui::EndChild();

    ImGui::End();
}

void ContentBrowserPanel::renderRootSelector()
{
    ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "Content Roots");
    ImGui::Separator();

    if (_contentRoots.empty())
    {
        ImGui::TextColored(ImVec4(1, 0.5f, 0, 1), "No content\ndirectories\nfound");
        return;
    }

    // Vertical list of content roots
    for (auto &root : _contentRoots)
    {
        const char *icon  = "";
        ImVec4      color = ImVec4(1, 1, 1, 1);

        switch (root.type)
        {
        case ContentRootType::Engine:
            icon  = "[E]";
            color = ImVec4(0.3f, 0.7f, 1.0f, 1.0f); // Light blue
            break;
        case ContentRootType::Project:
            icon  = "[P]";
            color = ImVec4(0.3f, 1.0f, 0.3f, 1.0f); // Green
            break;
        case ContentRootType::Plugin:
            icon  = "[+]";
            color = ImVec4(1.0f, 0.7f, 0.3f, 1.0f); // Orange
            break;
        }

        bool isSelected = (_activeRoot == &root);

        // Highlight selected root
        if (isSelected)
        {
            ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(0.3f, 0.5f, 0.8f, 0.8f));
        }

        ImGui::PushID(&root);

        // Draw icon with color
        ImGui::TextColored(color, "%s", icon);
        ImGui::SameLine();

        // Selectable root name
        if (ImGui::Selectable(root.name.c_str(), isSelected, ImGuiSelectableFlags_AllowDoubleClick))
        {
            switchToRoot(&root);
        }

        ImGui::PopID();

        if (isSelected)
        {
            ImGui::PopStyleColor();
        }
    }
}

void ContentBrowserPanel::renderDirectoryContents()
{
    YA_PROFILE_FUNCTION();
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
                    // Only navigate if within active root
                    if (isPathWithinActiveRoot(path))
                    {
                        _currentDirectory = path;
                    }
                }
                else if (entry.path().string().ends_with(".scene.json")) {
                    // Open scene file in editor
                    App::get()->taskManager.registerFrameTask(
                        [scenePath = entry.path().string()]() {
                            App::get()->loadScene(scenePath);
                        });
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