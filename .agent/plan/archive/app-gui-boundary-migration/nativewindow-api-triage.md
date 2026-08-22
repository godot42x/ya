# Phase A NativeWindow API Triage

> 更新时间：2026-08-14
> 作用：把当前 `Foundation/RHI/NativeWindow.h` 的混合接口拆成三类：真正属于窗口对象的、真正属于 present bridge 的、以及不该继续留在这个接口上的 orchestration/policy。

## 1. 当前问题

`INativeWindow` 现在把三层语义混在一起：

1. 窗口对象身份与窗口级状态；
2. Vulkan present / surface bridge；
3. host/manager 级生命周期编排便利接口。

这会带来两个直接后果：

- RHI 公开面被完整窗口对象污染；
- GUI host / presenter / window manager 的 owner 边界说不清。

本表的目标不是马上改代码，而是先固定迁移口径。

## 2. 三分表

| 当前 API | 建议归属 | 动作 | 原因 / 约束 |
|---|---|---|---|
| `getNativeWindowHandle()` | `INativeWindow` | 保留 | 原生句柄属于窗口对象身份；GUI host、平台 glue、必要时 editor embedding 可读取，但 RHI 不应依赖它推导 surface policy |
| `setTitle(const std::string&)` | `INativeWindow` | 保留 | 窗口标题是窗口级状态，不属于 present bridge |
| `getWindowID()` | `INativeWindow` | 保留 | 窗口身份与 window routing 键，属于 window object |
| `getWindowSize(int&, int&)` | `INativeWindow` | 保留 | 窗口客户区尺寸是 window state；presenter 读取它决定 extent，但接口 owner 仍是 window |
| `setWindowSize(int, int)` | `INativeWindow` | 保留 | 窗口级命令；不是 RHI/presenter 合同 |
| `onCreateVkSurface(...)` | `IPresentSurfaceSource` | 下沉 | 这是 Vulkan surface bridge；RHI/presenter 需要的是“如何创建 surface”，不是完整窗口对象 |
| `onDestroyVkSurface(...)` | `IPresentSurfaceSource` | 下沉 | 与 surface 生命周期有关，不属于窗口 identity |
| `onGetVkInstanceExtensions()` | `IPresentSurfaceSource` | 下沉 | Vulkan required instance extensions 是 present bridge 需求，不是一般窗口 API |
| `init()` | concrete window + `NativeWindowManager` / `GUIWindowHost` | 过渡保留，但不应成为 RHI 可见合同 | 当前实现仍需要它完成 SDL window 建立；长期可继续留在 concrete window 生命周期中，但调用者只应是 window owner，不是任意消费者 |
| `destroy()` | concrete window + `NativeWindowManager` / `GUIWindowHost` | 过渡保留，但不应成为 RHI 可见合同 | 销毁顺序要由 host/manager 控制；RHI 只看 surface/presenter 释放结果 |
| `recreate(const WindowCreateInfo&)` | `NativeWindowManager` / `GUIWindowHost` / presenter-safe-point orchestration | 不应原样保留为通用窗口接口 | `recreate()` 混合了窗口重建、surface 失效、present target rebuild；真正的 safe-point 与 rebuild policy 不该塞在单个窗口接口里 |

## 3. 正式接口边界

### 3.1 `INativeWindow`

只表达：

- 窗口身份：`window id`、原生句柄；
- 窗口级状态：title、client size、DPI 等；
- 被 owner 调用的窗口生命周期实现。

它不再默认承担：

- Vulkan required extensions；
- Vulkan surface create/destroy；
- presenter rebuild policy；
- whole-app activation / modal / dragdrop 等高层策略。

### 3.2 `IPresentSurfaceSource`（命名仍可调整）

只表达：

- 创建/销毁图形后端的 present surface；
- 暴露创建该 surface 所需的最小平台扩展信息。

RHI / presenter 最终只能依赖这一层，而不是 `INativeWindow` 的完整接口。

### 3.3 `NativeWindowManager` / `GUIWindowHost` / presenter

这层负责：

- 窗口集合 owner；
- main window / active window 概念；
- resize 与重建安全点；
- presenter/swapchain/compose target 重建顺序；
- 多窗口策略与 app-level routing。

它们不该再把这些 policy 伪装成 `INativeWindow::recreate()` 的一个成员函数。

## 4. 调用者依赖规则

| 调用者 | 允许依赖 | 不允许依赖 |
|---|---|---|
| RHI / backend presenter | `IPresentSurfaceSource` | `setTitle()`、`getWindowID()`、window activation / modal / dragdrop policy |
| `GUIWindowHost` | `INativeWindow` + `IPresentSurfaceSource` | 直接把高层 app policy 下沉到 window 对象 |
| `NativeWindowManager` | `INativeWindow`（以及 concrete create/destroy 生命周期） | Vulkan surface policy 细节 |
| App form / product shell | 更高层 host 抽象 | 直接操作 Vulkan surface hooks |

## 5. 对下一轮迁移设计的直接结论

1. `NativeWindow.h` 不应继续作为 `ya-rhi` 的长期公开头之一；至少要把 present bridge 抽离到更窄的公开面。
2. 第一轮 move/rename 设计里，`INativeWindow` 与 `IPresentSurfaceSource` 可以先保持同一 concrete 类型（例如 SDL concrete），但 public header 与 target 归属必须先分语义。
3. `recreate()` 是当前最模糊的成员：下一轮设计要优先把它拆成“窗口对象更新”与“host/presenter 安全点重建”两层，而不是继续让调用者通过一个黑箱方法赌顺序。
4. `NativeWindowManager` 保留；需要移除的是 `Provider` / present hook 越界，不是 manager 这一 owner 概念。
