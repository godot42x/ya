#include "ContentBrowserPanel.h"
#include <imgui.h>
#include <filesystem>

namespace ya {

ContentBrowserPanel::ContentBrowserPanel()
    : _currentDirectory(std::filesystem::current_path())
{
}

void ContentBrowserPanel::onImGuiRender()
{
    ImGui::Begin("Content Browser", nullptr);

    if (_currentDirectory != std::filesystem::current_path())
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
    static constexpr float padding = 16.0f;
    static constexpr float cellSize = 128.0f + padding;

    float panelWidth = ImGui::GetContentRegionAvail().x;
    int columnCount = static_cast<int>(panelWidth / cellSize);
    if (columnCount < 1)
        columnCount = 1;

    ImGui::Columns(columnCount, 0, false);

    for (auto& entry : std::filesystem::directory_iterator(_currentDirectory))
    {
        const auto& path = entry.path();
        auto filename = path.filename().string();

        ImGui::PushID(filename.c_str());

        if (entry.is_directory())
        {
            ImGui::Text("[DIR] %s", filename.c_str());
            if (ImGui::IsItemClicked())
            {
                _currentDirectory /= filename;
            }
        }
        else
        {
            ImGui::Text("[FILE] %s", filename.c_str());
        }

        ImGui::PopID();
        ImGui::NextColumn();
    }

    ImGui::Columns(1);
}

} // namespace ya
