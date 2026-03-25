# GitHub Copilot 项目指南

## 项目概览

**项目名称**: YA Engine (Yet Another Engine)  
**项目类型**: C++ 游戏引擎  
**构建系统**: XMake  
**渲染后端**: Vulkan + OpenGL (多后端架构)


## SKILLS

查看 `.github/skills/` 下的每一个 `SKILL.md` 作为索引。

- SOUL : `./skills/soul/SKILL.md`
- BUILD : `./skills/build/SKILL.md`
- MATERIAL_FLOW : `./skills/material-flow/SKILL.md`
- RENDER_ARCH : `./skills/render-arch/SKILL.md`
- CPP_STYLE : `./skills/cpp-style/SKILL.md`
- DEBUG_REVIEW : `./skills/debug-review/SKILL.md`
- VSCODE : `./skills/vscode/SKILL.md`

### Skill 抉择方案（触发条件 -> 选择）

1. **需求不明确 / 语义含糊** -> 先用 **SOUL** 做问题澄清，再进入具体技能。
2. **构建、运行、目标选择、编译报错** -> 优先 **BUILD**。
3. **ECS -> 材质 -> 渲染管线的数据流、唯一数据源、editor 属性上报、shared material、resolve/upload 边界** -> 优先 **MATERIAL_FLOW**。
4. **渲染管线/后端/材质系统边界问题** -> 优先 **RENDER_ARCH**。
5. **C++ 新增/重构/风格统一/生命周期约束** -> 优先 **CPP_STYLE**。
6. **崩溃排查、日志定位、提交前自检/Review** -> 优先 **DEBUG_REVIEW**。
7. **VS Code 任务/调试/插件/settings/clangd 配置** -> 优先 **VSCODE**。

### 优先级与冲突规则

- 单一任务命中多个技能时，按：`BUILD > VSCODE > MATERIAL_FLOW > RENDER_ARCH > CPP_STYLE > DEBUG_REVIEW > SOUL`。
- 若问题本身不清晰，先执行 `SOUL` 澄清，再按上面的优先级切换。
- 一个任务可串行切换多个技能，但每一步只使用一个主技能，避免规则混杂。

### 退出与回退规则

- 当前技能达到“退出条件”后，切换到下一主技能。
- 当前技能无法推进时，回退到：
  - 技术问题回退 `BUILD`（先保证可验证）
  - 需求问题回退 `SOUL`（先澄清约束）

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

### 构建

**直接使用 XMake：**

```bash
xmake                    # 构建
xmake run TargetName     # 运行指定目标
xmake clean             # 清理
xmake config            # 配置
```

- 如何排查编译报错:
通过 `grep`, `select-string` 等 shell 工具, 来过滤非 error 的输出

### 项目目标

主要在 Examples 目录之下，其他目标可以通过 `xmake l targets` 查看。或者搜索xmake.lua中的`target("TargetName")`。

- `HelloMaterial` - 材质示例

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
│   ├── Plugins/            # 插件系统
│   ├── Programs/           # 编辑器/工具
│   └── Intermediate/       # 生成中间文件
├── Example/                # 示例项目
│   ├── GreedSnake/
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
       └── IRenderPass (可选, 兼容/传统路径)
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

- 类的编写: 所有类的成员变量应该在最上方进行声明, 关注数据而不是方法
- 如果不能了解所有的声明周期，禁止使用 new/delete, 统一使用智能指针

### 内存管理

- **智能指针优先**: 使用 `std::shared_ptr` / `std::unique_ptr`
- **接口返回**: 优先返回 `shared_ptr`，避免裸指针

常用缩写:
- `stdptr<T>` - `std::shared_ptr<T>`
- `makeshared<T>(...)` - `std::make_shared<T>(...)`


## 调试信息

### 常见崩溃点

1. **Access Violation (0xC0000005)**: 通常是悬空指针或空指针解引用
2. **初始化顺序**: 确保 `IRender` 在使用前已初始化
3. **资源生命周期**: 检查 `shared_ptr` 引用计数
4. vk device lost:
上一帧的资源，在下一帧在了构了。帧录制中途，执行了某个recreate操作， 这些都应该在下一帧开始做

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
4. 禁止随意生成任意文档或md(除非用户主动要求), 或者多余的注释，优秀的代码说明一切(加上必要的注释)


## Git 提交规则

- 提交信息使用前缀格式: `[xxx] message`，允许多个前缀，例如 `[rhi x material] init pipeline wiring`
- 细分模块时可用子项前缀，例如 `[material/phong]`、`[material/unlit]`
- 能够清楚区分主要修改模块即可，不要拆得太细


