# GitHub Copilot 项目指南

## 项目概览

**项目名称**: YA Engine (Yet Another Engine)  
**项目类型**: C++ 游戏引擎  
**构建系统**: XMake  
**渲染后端**: Vulkan + OpenGL (多后端架构)

## 构建系统

### XMake 配置

项目使用 **XMake** 作为构建系统，配置文件为 `xmake.lua`。

### 常用命令

**通过 Makefile 包装器：**

阅读项目根目录下的 `Makefile`，使用以下命令：
```bash

# example, maybe break-change
make  r t=TargetName   # 运行指定目标


```

**直接使用 XMake：**

```bash
xmake                    # 构建
xmake run TargetName     # 运行指定目标
xmake clean             # 清理
xmake config            # 配置
```

### 项目目标

主要在 Examples 目录之下，其他目标可以通过 `xmake l targets` 查看。或者搜索xmake.lua中的`target("TargetName")`。

- `GreedySnake` - 贪吃蛇示例
- `HelloMaterial` - 材质示例
- `Sandbox` - 沙盒测试环境

## 架构设计

### 渲染后端

项目采用多渲染后端架构：

1. **IRender** - 渲染接口抽象层
2. **VulkanRender** - Vulkan 实现 (主要后端)
3. **OpenGLRender** - OpenGL 实现 (新增)

### 关键目录结构

```
Neon/
├── Engine/
│   ├── Source/
│   │   ├── Core/           # 核心系统
│   │   │   └── App/        # 应用框架
│   │   ├── Platform/       # 平台相关代码
│   │   │   └── Render/
│   │   │       ├── Vulkan/ # Vulkan 后端
│   │   │       └── OpenGL/ # OpenGL 后端
│   │   ├── Render/         # 渲染抽象层
│   │   ├── ECS/            # 实体组件系统
│   │   └── Scene/          # 场景管理
│   ├── Shader/             # 着色器源码
│   ├── Content/            # 资源文件
│   └── Plugins/            # 插件系统
├── Example/                # 示例项目
│   ├── GreedySnake/
│   ├── HelloMaterial/
│   └── Sandbox/
└── xmake.lua              # XMake 配置
```

### 核心类关系

```
App
  └── IRender (直接持有)
       ├── ISwapchain
       ├── ICommandBuffer
       └── IRenderPass
  └── IRenderTarget (shared_ptr)
       └── MaterialSystems
  └── SceneManager
  └── ImGuiManager
```

## Render2D 相关

### 坐标系

左上角为原点，X 轴向右，Y 轴向下。

Render2D:drawTexture(),  会以传入的位置为左上角， size 向右下扩展来进行渲染.


## 编码规范

### 命名约定

- **类/结构体**: PascalCase (如 `VulkanRender`, `IRenderTarget`)
- **成员变量**: 
  - 私有: `_memberName` (下划线前缀)
  - 公有: `memberName` (无前缀)
- **局部变量**: camelCase
- **函数**: camelCase
- **常量**: UPPER_SNAKE_CASE

### 内存管理

- **智能指针优先**: 使用 `std::shared_ptr` / `std::unique_ptr`
- **接口返回**: 优先返回 `shared_ptr`，避免裸指针

常用缩写:
- `stdptr<T>` - `std::shared_ptr<T>`
- `makeshared<T>(...)` - `std::make_shared<T>(...)`


## 最近的重构

### 已移除的类

- ~~`RenderContext`~~ - 已移除，功能合并到 `App` 直接管理 `IRender`

### 已修复的问题

- **悬空指针**: `_rt` 从裸指针改为 `shared_ptr<IRenderTarget>`
- **生命周期管理**: 确保所有资源正确使用智能指针

## 调试信息

### 常见崩溃点

1. **Access Violation (0xC0000005)**: 通常是悬空指针或空指针解引用
2. **初始化顺序**: 确保 `IRender` 在使用前已初始化
3. **资源生命周期**: 检查 `shared_ptr` 引用计数

### 日志级别

使用 `YA_CORE_*` 系列宏：
- `YA_CORE_TRACE` - 详细跟踪
- `YA_CORE_DEBUG` - 调试信息
- `YA_CORE_INFO` - 一般信息
- `YA_CORE_WARN` - 警告
- `YA_CORE_ERROR` - 错误
- `YA_CORE_ASSERT` - 断言

## 第三方依赖

- **SDL3**: 窗口管理
- **Vulkan SDK**: Vulkan 渲染
- **GLAD**: OpenGL 加载器
- **GLM**: 数学库
- **ImGui**: UI 库
- **SPIRV-Cross**: 着色器交叉编译

## 重要提示

⚠️ **永远记住**：
1. 项目使用 XMake，不是 CMake
2. 优先使用智能指针管理资源
3. 遵循现有的接口抽象层设计
