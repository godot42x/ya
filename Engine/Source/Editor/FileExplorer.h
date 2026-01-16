#pragma once

#include <filesystem>
#include <functional>
#include <string>
#include <vector>

#include "Editor/EditorCommon.h"

namespace ya
{

struct ImGuiImageEntry;

/**
 * @brief 可复用的层级文件浏览器组件
 *
 * 提供类似 ContentBrowser 的界面：
 * - 左侧：Mount point 列表（Content Roots）
 * - 右侧：当前目录内容（支持层级导航）
 * - 支持图标/列表两种展示模式
 *
 * 可被 FilePicker、ContentBrowserPanel 等复用
 */
class FileExplorer
{
  public:
    enum class FilterMode
    {
        Files,       // 只显示文件
        Directories, // 只显示目录
        Both         // 显示文件和目录
    };

    enum class SelectionMode
    {
        File,     // 选择文件
        Directory // 选择目录
    };

    enum class ViewMode
    {
        List, // 列表视图（默认）
        Icon  // 图标视图（类似 ContentBrowser）
    };

    struct MountPoint
    {
        std::string           name; // Display name ("Engine", "Game", etc.)
        std::filesystem::path path; // Physical path
        bool                  isActive = false;
    };

    struct Icons
    {
        const ImGuiImageEntry *folder = nullptr;
        const ImGuiImageEntry *file   = nullptr;
    };

    using SelectionCallback  = std::function<void(const std::filesystem::path &)>;
    using ItemActionCallback = std::function<void(const std::filesystem::path &)>;

    FileExplorer()  = default;
    ~FileExplorer() = default;

    /**
     * @brief 初始化文件浏览器
     * @param mountPoints 挂载点列表
     * @param extensions 文件扩展名过滤（空表示所有文件）
     * @param filterMode 过滤模式
     * @param selectionMode 选择模式（选文件还是选目录）
     */
    void init(const std::vector<MountPoint>  &mountPoints,
              const std::vector<std::string> &extensions    = {},
              FilterMode                      filterMode    = FilterMode::Both,
              SelectionMode                   selectionMode = SelectionMode::File);

    /**
     * @brief 从 VirtualFileSystem 自动发现挂载点
     */
    void initFromVFS();

    /**
     * @brief 渲染文件浏览器界面
     * @param onSelect 选择回调（双击或确认时调用）
     * @param height 高度（-1 表示自动填充）
     */
    void render(SelectionCallback onSelect = nullptr, float height = -1);

    /**
     * @brief 获取当前选中的路径
     */
    const std::filesystem::path &getSelectedPath() const { return _selectedPath; }

    /**
     * @brief 设置当前选中的路径
     */
    void setSelectedPath(const std::filesystem::path &path);

    /**
     * @brief 获取当前目录
     */
    const std::filesystem::path &getCurrentDirectory() const { return _currentDirectory; }

    /**
     * @brief 获取当前激活的挂载点
     */
    const MountPoint *getActiveMountPoint() const { return _activeMountPoint; }

    /**
     * @brief 设置扩展名过滤
     */
    void setExtensions(const std::vector<std::string> &extensions) { _extensions = extensions; }

    /**
     * @brief 设置过滤模式
     */
    void setFilterMode(FilterMode mode) { _filterMode = mode; }

    /**
     * @brief 设置选择模式
     */
    void setSelectionMode(SelectionMode mode) { _selectionMode = mode; }

    /**
     * @brief 设置左侧面板宽度
     */
    void setLeftPanelWidth(float width) { _leftPanelWidth = width; }

    /**
     * @brief 设置视图模式
     */
    void setViewMode(ViewMode mode) { _viewMode = mode; }

    /**
     * @brief 获取视图模式
     */
    ViewMode getViewMode() const { return _viewMode; }

    /**
     * @brief 设置图标（用于图标视图）
     */
    void setIcons(const Icons &icons) { _icons = icons; }

    /**
     * @brief 设置图标视图的缩略图大小
     */
    void setThumbnailSize(float size) { _thumbnailSize = size; }

    /**
     * @brief 设置图标视图的内边距
     */
    void setPadding(float padding) { _padding = padding; }

    /**
     * @brief 设置文件双击回调（用于打开文件等操作）
     */
    void setItemActionCallback(ItemActionCallback callback) { _itemActionCallback = callback; }

    /**
     * @brief 判断路径是否匹配扩展名过滤
     */
    bool matchesExtension(const std::filesystem::path &path) const;

    /**
     * @brief 是否显示视图切换按钮
     */
    void setShowViewModeToggle(bool show) { _showViewModeToggle = show; }

    /**
     * @brief 是否显示尺寸调节滑块（图标模式下）
     */
    void setShowSizeSlider(bool show) { _showSizeSlider = show; }

  private:
    std::vector<MountPoint> _mountPoints;
    MountPoint             *_activeMountPoint = nullptr;
    std::filesystem::path   _currentDirectory;
    std::filesystem::path   _selectedPath;

    std::vector<std::string> _extensions;
    FilterMode               _filterMode    = FilterMode::Both;
    SelectionMode            _selectionMode = SelectionMode::File;
    ViewMode                 _viewMode      = ViewMode::List;

    float _leftPanelWidth    = 150.0f;
    char  _searchBuffer[128] = "";

    // Icon view settings
    Icons _icons;
    float _thumbnailSize      = 94.0f;
    float _padding            = 16.0f;
    bool  _showViewModeToggle = true;
    bool  _showSizeSlider     = true;

    // Callbacks
    ItemActionCallback _itemActionCallback;

    void switchToMountPoint(MountPoint *mp);
    bool isPathWithinActiveMountPoint(const std::filesystem::path &path) const;
    void renderMountPointSelector();
    void renderDirectoryContents(SelectionCallback onSelect);
    void renderListView(SelectionCallback                                    onSelect,
                        const std::vector<std::filesystem::directory_entry> &directories,
                        const std::vector<std::filesystem::directory_entry> &files);
    void renderIconView(SelectionCallback                                    onSelect,
                        const std::vector<std::filesystem::directory_entry> &directories,
                        const std::vector<std::filesystem::directory_entry> &files);
    bool matchesSearch(const std::string &name) const;
};

} // namespace ya
