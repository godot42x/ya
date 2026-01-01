#include "FilePicker.h"
#include "Core/Log.h"
#include <algorithm>
#include <filesystem>
#include <imgui.h>

namespace ya
{

void FilePicker::open(const std::string& title,
                      const std::string& currentPath,
                      const std::vector<std::string>& rootDirs,
                      const std::vector<std::string>& extensions,
                      FilterMode filterMode,
                      Callback onConfirm)
{
    _isOpen = true;
    _title = title;
    _currentPath = currentPath;
    _selectedPath = currentPath;  // 初始选中当前路径
    _rootDirs = rootDirs;
    _extensions = extensions;
    _filterMode = filterMode;
    _onConfirm = onConfirm;
    
    // 清空搜索框
    memset(_searchBuffer, 0, sizeof(_searchBuffer));
    
    // 扫描文件/目录
    scanItems();
    
}

void FilePicker::close()
{
    _isOpen = false;
    _availableItems.clear();
    _onConfirm = nullptr;
}

void FilePicker::render()
{
    if (!_isOpen) return;
    

    ImGui::OpenPopup(_title.c_str());
    // 居中弹窗
    ImVec2 center = ImGui::GetMainViewport()->GetCenter();
    ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(500, 400), ImGuiCond_FirstUseEver);
    
    bool open = _isOpen;
    if (ImGui::BeginPopupModal(_title.c_str(), &open, ImGuiWindowFlags_NoResize))
    {
        renderContent();
        ImGui::EndPopup();
    }
    
    // 检查是否通过 X 按钮关闭
    if (!open) {
        close();
    }
}

void FilePicker::openScriptPicker(const std::string& currentPath, Callback onConfirm)
{
    open("Select Lua Script",
         currentPath,
         {"Engine/Content/Lua", "Content/Scripts"},
         {".lua"},
         FilterMode::Files,
         onConfirm);
}

void FilePicker::openMaterialPicker(const std::string& currentPath, Callback onConfirm)
{
    open("Select Material",
         currentPath,
         {"Engine/Content/Materials", "Content/Materials"},
         {".mat", ".material"},
         FilterMode::Files,
         onConfirm);
}

void FilePicker::openTexturePicker(const std::string& currentPath, Callback onConfirm)
{
    open("Select Texture",
         currentPath,
         {"Engine/Content/Textures", "Content/Textures"},
         {".png", ".jpg", ".jpeg", ".tga", ".bmp", ".dds", ".hdr"},
         FilterMode::Files,
         onConfirm);
}

void FilePicker::openDirectoryPicker(const std::string& currentPath,
                                    const std::vector<std::string>& rootDirs,
                                    Callback onConfirm)
{
    open("Select Directory",
         currentPath,
         rootDirs,
         {},
         FilterMode::Directories,
         onConfirm);
}

void FilePicker::scanItems()
{
    namespace fs = std::filesystem;
    
    _availableItems.clear();
    
    auto scanDirectory = [this](const fs::path& dir) {
        if (!fs::exists(dir)) return;
        
        try {
            for (const auto& entry : fs::recursive_directory_iterator(dir)) {
                bool isDir = entry.is_directory();
                bool isFile = entry.is_regular_file();
                
                // 根据过滤模式判断是否包含
                if (_filterMode == FilterMode::Files && !isFile) continue;
                if (_filterMode == FilterMode::Directories && !isDir) continue;
                
                std::string relativePath = entry.path().string();
                std::replace(relativePath.begin(), relativePath.end(), '\\', '/');
                
                // 文件扩展名过滤
                if (isFile && !_extensions.empty()) {
                    std::string ext = entry.path().extension().string();
                    std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
                    
                    bool matched = false;
                    for (const auto& allowedExt : _extensions) {
                        std::string lowerAllowed = allowedExt;
                        std::transform(lowerAllowed.begin(), lowerAllowed.end(), lowerAllowed.begin(), ::tolower);
                        if (ext == lowerAllowed) {
                            matched = true;
                            break;
                        }
                    }
                    if (!matched) continue;
                }
                
                // 添加标记区分文件和目录
                if (isDir) {
                    _availableItems.push_back("[DIR] " + relativePath);
                } else {
                    _availableItems.push_back(relativePath);
                }
            }
        } catch (const std::exception& e) {
            YA_CORE_WARN("Failed to scan directory {}: {}", dir.string(), e.what());
        }
    };
    
    // 扫描所有根目录
    for (const auto& rootDir : _rootDirs) {
        scanDirectory(rootDir);
    }
    
    // 排序
    std::sort(_availableItems.begin(), _availableItems.end());
}

void FilePicker::renderContent()
{
    // 显示过滤信息
    if (!_extensions.empty()) {
        std::string filterInfo = "Filter: ";
        for (size_t i = 0; i < _extensions.size(); ++i) {
            filterInfo += _extensions[i];
            if (i < _extensions.size() - 1) filterInfo += ", ";
        }
        ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "%s", filterInfo.c_str());
    } else {
        const char* modeText = _filterMode == FilterMode::Directories ? "Directories" :
                              _filterMode == FilterMode::Files ? "Files" : "All";
        ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "Mode: %s", modeText);
    }
    
    ImGui::Separator();
    
    // 搜索框
    ImGui::SetNextItemWidth(-1);
    ImGui::InputTextWithHint("##search", "Search...", _searchBuffer, sizeof(_searchBuffer));
    
    ImGui::Separator();
    
    // 文件/目录列表
    ImGui::BeginChild("ItemList", ImVec2(0, -30), true);
    
    std::string searchStr = _searchBuffer;
    std::transform(searchStr.begin(), searchStr.end(), searchStr.begin(), ::tolower);
    
    for (const auto& itemPath : _availableItems) {
        // 移除 [DIR] 前缀获取实际路径
        std::string actualPath = itemPath;
        bool isDir = false;
        if (actualPath.find("[DIR] ") == 0) {
            actualPath = actualPath.substr(6);
            isDir = true;
        }
        
        // 过滤搜索
        if (!searchStr.empty() && !matchesSearch(actualPath, searchStr)) {
            continue;
        }
        
        // 高亮临时选中的项
        bool isSelected = (actualPath == _selectedPath);
        if (isSelected) {
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.3f, 0.8f, 1.0f, 1.0f));
        } else if (isDir) {
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.9f, 0.4f, 1.0f));
        }
        
        // 显示名称
        std::string displayName = getDisplayName(actualPath);
        if (isDir) {
            displayName = "📁 " + displayName;
        }
        
        // 可选择项
        if (ImGui::Selectable(displayName.c_str(), isSelected, ImGuiSelectableFlags_AllowDoubleClick)) {
            _selectedPath = actualPath;
            
            // 双击直接确认
            if (ImGui::IsMouseDoubleClicked(0)) {
                if (_onConfirm) {
                    _onConfirm(_selectedPath);
                }
                close();
            }
        }
        
        if (isSelected || isDir) {
            ImGui::PopStyleColor();
        }
        
        if (isSelected) {
            ImGui::SetScrollHereY(0.5f);
        }
    }
    
    ImGui::EndChild();
    
    ImGui::Separator();
    
    // 底部按钮
    if (ImGui::Button("OK", ImVec2(120, 0))) {
        if (_onConfirm && !_selectedPath.empty()) {
            _onConfirm(_selectedPath);
        }
        close();
    }
    
    ImGui::SameLine();
    
    if (ImGui::Button("Cancel", ImVec2(120, 0))) {
        close();
    }
    
    ImGui::SameLine();
    ImGui::TextDisabled("(%zu items)", _availableItems.size());
}

bool FilePicker::matchesSearch(const std::string& path, const std::string& searchLower) const
{
    std::string lowerPath = path;
    std::transform(lowerPath.begin(), lowerPath.end(), lowerPath.begin(), ::tolower);
    return lowerPath.find(searchLower) != std::string::npos;
}

std::string FilePicker::getDisplayName(const std::string& path) const
{
    // 提取文件名或目录名
    size_t lastSlash = path.find_last_of('/');
    if (lastSlash != std::string::npos) {
        return path.substr(lastSlash + 1);
    }
    return path;
}

} // namespace ya
