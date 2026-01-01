# FilePicker - 通用资源选择器

## 概述

通用 ImGui 文件/资源选择器组件，支持文件、目录、多种资源类型。

## 核心特性

- ✅ 文件/目录/混合模式
- ✅ 扩展名过滤 (`.lua`, `.mat`, `.png`, etc.)
- ✅ 多根目录扫描
- ✅ 实时搜索
- ✅ 回调确认机制

## 快速开始

```cpp
// 在 Panel 中声明
class MyPanel {
    FilePicker _filePicker;
public:
    void onImGuiRender() {
        // 打开选择器
        if (ImGui::Button("Browse Script")) {
            _filePicker.openScriptPicker(currentPath, [this](const std::string& path) {
                loadScript(path);
            });
        }
        
        // 每帧渲染
        _filePicker.render();
    }
};
```

## API 参考

### 便捷方法

```cpp
// Lua 脚本选择器 (.lua)
_filePicker.openScriptPicker(currentPath, callback);

// 材质选择器 (.mat, .material)
_filePicker.openMaterialPicker(currentPath, callback);

// 纹理选择器 (.png, .jpg, .dds, .hdr)
_filePicker.openTexturePicker(currentPath, callback);

// 目录选择器
_filePicker.openDirectoryPicker(currentPath, rootDirs, callback);
```

### 通用方法

```cpp
void open(
    const std::string& title,
    const std::string& currentPath,
    const std::vector<std::string>& rootDirs,
    const std::vector<std::string>& extensions,
    FilterMode filterMode,
    Callback onConfirm
);
```

**FilterMode**: `Files` | `Directories` | `Both`

## 实现细节

- **扫描**: `std::filesystem::recursive_directory_iterator`
- **路径规范化**: Unix 风格 (`/`)
- **搜索**: 大小写不敏感
- **目录标识**: 📁 图标 + 黄色高亮

## 注意事项

1. **生命周期**: `FilePicker` 实例必须在使用期间有效
2. **render() 调用**: 必须在 ImGui 渲染循环中每帧调用
3. **回调时机**: 仅在点击 OK 或双击时触发
4. **线程安全**: 仅主线程使用

## 扩展建议

- [ ] 文件预览
- [ ] 最近使用列表
- [ ] 收藏夹
- [ ] 多选模式
- [ ] 文件大小/时间显示

void openScriptPicker(const std::string& currentPath, Callback onConfirm);
```

自动配置:
- 目录: `Engine/Content/Lua`, `Content/Scripts`
- 扩展名: `.lua`

#### 材质选择器

```cpp
void openMaterialPicker(const std::string& currentPath, Callback onConfirm);
```

自动配置:
- 目录: `Engine/Content/Materials`, `Content/Materials`
- 扩展名: `.mat`, `.material`

#### 纹理选择器

```cpp
void openTexturePicker(const std::string& currentPath, Callback onConfirm);
```

自动配置:
- 目录: `Engine/Content/Textures`, `Content/Textures`
- 扩展名: `.png`, `.jpg`, `.jpeg`, `.tga`, `.bmp`, `.dds`, `.hdr`

#### 目录选择器

```cpp
void openDirectoryPicker(const std::string& currentPath,
                        const std::vector<std::string>& rootDirs,
                        Callback onConfirm);
```

### 其他方法

```cpp
void render();  // 每帧调用以渲染弹窗
void close();   // 手动关闭弹窗
```

## 使用示例

### 基础用法 - Lua 脚本选择器

```cpp
class MyPanel {
    FilePicker _filePicker;
    
public:
    void onImGuiRender() {
        if (ImGui::Button("Browse Script...")) {
            _filePicker.openScriptPicker(currentScriptPath, [this](const std::string& newPath) {
                currentScriptPath = newPath;
                reloadScript();
            });
        }
        
        // 在每帧末尾渲染
        _filePicker.render();
    }
};
```

### 自定义资源选择器

```cpp
// 音频文件选择器
_filePicker.open("Select Audio File",
                currentAudioPath,
                {"Engine/Content/Audio", "Content/Audio"},
                {".wav", ".mp3", ".ogg"},
                FilePicker::FilterMode::Files,
                [this](const std::string& path) {
                    loadAudio(path);
                });
```

### 目录选择器

```cpp
_filePicker.openDirectoryPicker(
    currentProjectDir,
    {"Content", "Engine/Content"},
    [this](const std::string& dirPath) {
        setProjectDirectory(dirPath);
    });
```

## UI 特性

- **搜索框**: 实时过滤文件/目录列表
- **双击确认**: 双击项目直接确认选择
- **高亮显示**: 当前路径和临时选中项高亮
- **目录标识**: 目录项显示 📁 图标和黄色文字
- **OK/Cancel**: 明确的确认和取消按钮

## 注意事项

1. **回调时机**: 只有点击 OK 按钮或双击项目时才会触发 `onConfirm` 回调
2. **生命周期**: `FilePicker` 实例需要在使用期间保持有效
3. **render() 调用**: 必须在 ImGui 渲染循环中调用 `render()`
4. **线程安全**: 仅在主线程使用

## 实现细节

- **目录扫描**: 使用 `std::filesystem::recursive_directory_iterator`
- **路径规范化**: 自动转换为 Unix 风格路径（`/`）
- **大小写不敏感**: 搜索和扩展名匹配不区分大小写
- **性能**: 打开时一次性扫描，缓存结果直到关闭

## 迁移指南

### 从 ScriptFilePicker 迁移

**旧代码:**
```cpp
bool _showScriptPicker = false;
void* _scriptPickerTarget = nullptr;
std::vector<std::string> _availableScripts;

// 在按钮回调中
_showScriptPicker = true;
_scriptPickerTarget = &script;

// 手动渲染
if (_showScriptPicker && _scriptPickerTarget == &script) {
    renderScriptFilePicker(&script);
}
```

**新代码:**
```cpp
FilePicker _filePicker;

// 在按钮回调中
_filePicker.openScriptPicker(script.scriptPath, [&script](const std::string& newPath) {
    script.scriptPath = newPath;
    script.bLoaded = false;
});

// 统一渲染
_filePicker.render();
```

## 扩展建议

未来可以添加:
- 文件预览功能
- 最近使用的文件列表
- 收藏夹/书签
- 多选模式
- 自定义图标
- 文件大小/修改时间显示
