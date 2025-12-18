# 文件系统挂载点使用指南

## 概述

改进后的文件系统支持灵活的虚拟路径挂载机制，便于管理游戏资源。

## 挂载点类型

### 1. 引擎资源 (Engine/)
自动挂载到 `Engine/` 目录
```cpp
// "Engine/Shader/Basic.glsl" -> "ProjectRoot/Engine/Shader/Basic.glsl"
auto shader = loadShader("Engine/Shader/Basic.glsl");
```

### 2. 游戏根目录 (Game Root)
通过 `setGameRoot()` 设置当前游戏的根目录，自动挂载以下子目录：
- `Content/` - 游戏资源（纹理、模型、音频等）
- `Config/` - 配置文件
- `Save/` - 存档文件

```cpp
void onInit(ya::AppDesc ci) override
{
    Super::onInit(ci);
    
    // 设置游戏根目录
    auto gameRoot = FileSystem::get()->getProjectRoot() / "Example" / "GreedySnake";
    FileSystem::get()->setGameRoot(gameRoot);
}
```

### 3. 自定义挂载点
通过 `mount()` 注册自定义虚拟路径

```cpp
// 挂载 Mod 目录
FileSystem::get()->mount("Mods", "C:/Users/Player/Mods");

// 挂载共享资源
FileSystem::get()->mount("SharedAssets", projectRoot / "SharedAssets");

// 使用挂载点
auto texture = loadTexture("Mods/CoolMod/texture.png");
auto config = loadConfig("SharedAssets/global_config.json");
```

## 使用示例

### 游戏入口点设置

```cpp
// Example/GreedySnake/Source/GreedySnake.h
struct GreedySnake : public ya::App
{
    void onInit(ya::AppDesc ci) override
    {
        Super::onInit(ci);
        
        // 方法1: 相对于项目根目录
        auto gameRoot = FileSystem::get()->getProjectRoot() / "Example" / "GreedySnake";
        FileSystem::get()->setGameRoot(gameRoot);
        
        // 方法2: 使用预处理器宏 (推荐)
        #ifdef GAME_ROOT_PATH
        FileSystem::get()->setGameRoot(GAME_ROOT_PATH);
        #endif
        
        // 方法3: 从配置文件读取
        // auto config = loadGameConfig();
        // FileSystem::get()->setGameRoot(config.rootPath);
    }
};
```

### 资源加载示例

```cpp
// 加载游戏内容
auto sceneData = loadScene("Content/Scene.json");
auto playerSprite = loadTexture("Content/Textures/player.png");
auto bgMusic = loadAudio("Content/Audio/background.mp3");

// 加载配置
auto gameConfig = loadConfig("Config/game.ini");

// 加载/保存存档
auto saveData = loadSave("Save/slot1.sav");
saveSave("Save/slot1.sav", saveData);
```

### 高级用法：动态 Mod 系统

```cpp
void loadMods()
{
    auto modsDir = std::filesystem::path("C:/Users/Player/AppData/Mods");
    
    for (auto& entry : std::filesystem::directory_iterator(modsDir))
    {
        if (entry.is_directory())
        {
            std::string modName = entry.path().filename().string();
            FileSystem::get()->mount("Mod_" + modName, entry.path());
            
            // 加载 Mod 内容
            loadModContent("Mod_" + modName + "/manifest.json");
        }
    }
}

// 卸载 Mod
void unloadMod(const std::string& modName)
{
    FileSystem::get()->unmount("Mod_" + modName);
}
```

## 路径解析优先级

1. `Engine/` 前缀 → Engine 目录
2. `Content/`, `Config/`, `Save/` 前缀 → Game Root 子目录
3. 自定义挂载点匹配
4. 相对于 Project Root

## API 参考

```cpp
class FileSystem
{
    // 设置游戏根目录 (自动挂载 Content/Config/Save)
    void setGameRoot(const stdpath &path);
    
    // 注册自定义挂载点
    void mount(const std::string &mountName, const stdpath &physicalPath);
    
    // 移除挂载点
    void unmount(const std::string &mountName);
    
    // 虚拟路径转换为物理路径
    stdpath translatePath(std::string_view virtualPath) const;
    
    // 读取文件为字符串
    bool readFileToString(std::string_view filepath, std::string &output) const;
    
    // 检查文件是否存在
    bool isFileExists(const std::string &filepath) const;
};
```

## 最佳实践

1. **在游戏初始化时设置 GameRoot**：确保所有 Content 访问正确
2. **使用虚拟路径**：避免硬编码绝对路径
3. **为不同资源类型创建挂载点**：便于管理和热重载
4. **Mod 系统使用独立挂载点**：避免命名冲突
5. **配置文件独立挂载**：便于多平台适配

## 示例项目结构

```
Neon/
├── Engine/
│   ├── Shader/
│   └── Content/
└── Example/
    └── GreedySnake/
        ├── Content/          # 自动挂载为 "Content/"
        │   ├── Textures/
        │   ├── Audio/
        │   └── Scene.json
        ├── Config/           # 自动挂载为 "Config/"
        │   └── game.ini
        └── Save/             # 自动挂载为 "Save/"
            └── slot1.sav
```
