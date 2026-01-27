#pragma once

#include "FileExplorer.h"
#include <functional>
#include <string>
#include <vector>

namespace ya
{

struct ImGuiImageEntry;

/**
 * @brief 通用文件/资源选择器弹窗
 *
 * 基于 FileExplorer 组件实现层级目录浏览
 * 支持选择多种类型的资源：
 * - Lua 脚本 (.lua)
 * - 材质 (.mat)
 * - 纹理 (.png, .jpg, .dds, etc.)
 * - 模型 (.obj, .fbx, .gltf, etc.)
 * - 目录
 */
struct FilePicker
{
  public:
    using Callback     = std::function<void(const std::string &)>;
    using SaveCallback = std::function<void(const std::string &dir, const std::string &filename)>;

  private:
    bool        _isOpen       = false;
    bool        _pendingClose = false;
    std::string _title        = "Select File";

    FileExplorer _fileExplorer;
    Callback     _onConfirm;
    SaveCallback _onSaveConfirm;

    // Icons
    FileExplorer::Icons    _icons;
    FileExplorer::ViewMode _defaultViewMode = FileExplorer::ViewMode::Icon;

    // Scene save mode
    bool _bSceneSaveMode       = false;
    char _sceneNameBuffer[128] = "NewScene";

  public:

    FilePicker()  = default;
    ~FilePicker() = default;

    /**
     * @brief 设置图标（在 init 阶段调用，所有弹窗共享）
     */
    void setIcons(const ImGuiImageEntry *folderIcon, const ImGuiImageEntry *fileIcon);

    /**
     * @brief 设置默认视图模式
     */
    void setDefaultViewMode(FileExplorer::ViewMode mode) { _defaultViewMode = mode; }

    /**
     * @brief 打开文件选择器
     * @param title 弹窗标题
     * @param currentPath 当前选中的路径（用于高亮显示）
     * @param extensions 文件扩展名过滤（如 {".lua", ".mat"}），空表示所有文件
     * @param onConfirm 确认选择后的回调函数
     */
    void open(const std::string              &title,
              const std::string              &currentPath,
              const std::vector<std::string> &extensions,
              Callback                        onConfirm);

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
    void openScriptPicker(const std::string &currentPath, Callback onConfirm);

    /**
     * @brief 打开材质选择器
     */
    void openMaterialPicker(const std::string &currentPath, Callback onConfirm);

    /**
     * @brief 打开纹理选择器
     */
    void openTexturePicker(const std::string &currentPath, Callback onConfirm);

    /**
     * @brief 打开模型选择器
     */
    void openModelPicker(const std::string &currentPath, Callback onConfirm);

    /**
     * @brief 打开目录选择器
     */
    void openDirectoryPicker(const std::string &currentPath,
                             Callback           onConfirm);

    /**
     * @brief 打开 Scene 保存选择器（支持自定义文件名）
     */
    void openSceneSavePicker(const std::string &defaultName,
                             SaveCallback       onConfirm);


    void applyCommonSettings();
    void renderFileSelectContent();
    void renderSceneSaveContent();
};

} // namespace ya
