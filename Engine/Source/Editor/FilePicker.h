#pragma once

#include <functional>
#include <string>
#include <vector>

namespace ya
{

/**
 * @brief 通用文件/资源选择器
 * 
 * 支持选择多种类型的资源：
 * - Lua 脚本 (.lua)
 * - 材质 (.mat)
 * - 纹理 (.png, .jpg, .dds, etc.)
 * - 模型 (.obj, .fbx, .gltf, etc.)
 * - 目录
 */
class FilePicker
{
  public:
    enum class FilterMode
    {
        Files,       // 只显示文件
        Directories, // 只显示目录
        Both         // 显示文件和目录
    };

    using Callback = std::function<void(const std::string&)>;

    FilePicker() = default;
    ~FilePicker() = default;

    /**
     * @brief 打开文件选择器
     * @param title 弹窗标题
     * @param currentPath 当前选中的路径（用于高亮显示）
     * @param rootDirs 根目录列表（如 {"Engine/Content", "Content"}）
     * @param extensions 文件扩展名过滤（如 {".lua", ".mat"}），空表示所有文件
     * @param filterMode 过滤模式
     * @param onConfirm 确认选择后的回调函数
     */
    void open(const std::string& title,
              const std::string& currentPath,
              const std::vector<std::string>& rootDirs,
              const std::vector<std::string>& extensions,
              FilterMode filterMode,
              Callback onConfirm);

    /**
     * @brief 关闭选择器
     */
    void close();

    /**
     * @brief 渲染选择器界面（需要每帧调用）
     */
    void render();

    /**
     * @brief 检查是否打开
     */
    bool isOpen() const { return _isOpen; }

    // === 便捷方法 ===
    
    /**
     * @brief 打开 Lua 脚本选择器
     */
    void openScriptPicker(const std::string& currentPath, Callback onConfirm);
    
    /**
     * @brief 打开材质选择器
     */
    void openMaterialPicker(const std::string& currentPath, Callback onConfirm);
    
    /**
     * @brief 打开纹理选择器
     */
    void openTexturePicker(const std::string& currentPath, Callback onConfirm);
    
    /**
     * @brief 打开目录选择器
     */
    void openDirectoryPicker(const std::string& currentPath, 
                            const std::vector<std::string>& rootDirs,
                            Callback onConfirm);

  private:
    bool _isOpen = false;
    std::string _title = "Select File";
    std::string _currentPath;
    std::string _selectedPath;  // 临时选中路径（未确认）
    std::vector<std::string> _rootDirs;
    std::vector<std::string> _extensions;
    FilterMode _filterMode = FilterMode::Files;
    Callback _onConfirm;
    
    std::vector<std::string> _availableItems;
    char _searchBuffer[128] = "";

    void scanItems();
    void renderContent();
    bool matchesFilter(const std::string& path) const;
    bool matchesSearch(const std::string& path, const std::string& searchLower) const;
    std::string getDisplayName(const std::string& path) const;
};

} // namespace ya
