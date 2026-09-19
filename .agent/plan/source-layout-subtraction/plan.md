# Source 布局减法（S1–S5）

> 建立：2026-09-18
> 关联：`./.agent/skills/code-reorganize/SKILL.md`（头文件布局与 include 规则）
> 状态：S1、S5 已落地；S2 进行中

## 0. 问题

目录的**形状**（Framework / Applications 两层 + 每模块一个 target）是对的，但读者
依然找不到主流程。根因不是缺抽象，而是三层可删除的噪声加少数职责倒置：

1. `include/` 影子层：每个公开头存在两份（模块根真身 + `include/` 下 2 行转发
   stub），另有多路径别名。每次跳转定义、每次 `rg` 都先落到 stub 上。
2. 少数依赖倒置：`ya-scene-core -> ya-gui-widgets`、
   `ya-render-3d -> ya-gui-compose`、`ya-engine` 公开包含 `ya-game-runtime`、
   `ya-gui-framework` 拉入 AppKernel/AppControl/Hierarchy（"GUI framework" 其实
   是 GUI + app shell + module system）。
3. 体量集中：`EnvironmentLightingProcessor.cpp` 2558 行、`RenderGraph.cpp` 1987、
   `GUIAppHost.cpp` 1910、`WidgetTree.cpp` 1832、`UILayout.cpp` 1774。
4. 角色后缀泛滥：约 150 个 `*Context`/`*Services`/`*Host`/`*Section`/`*Registry`
   命名描述"在层蛋糕哪一层"，不描述"做什么"。
5. `.agent/plan` 自身膨胀到 22 个活跃目录，搜索真相时命中大量过时计划。

## 1. 边界（不做的事）

- 不新增抽象、不再造 bus / facade / 第三套生命周期。
- 不做"框架优化"立项：上面 5 条没有一条能靠新增抽象解决，全部靠删除。
- 不按行数机械拆文件；只在有明确接缝时拆。
- 不改行为、不改接口语义、不改资源时序。

## 2. 阶段

| 阶段 | 目标 | 状态 |
| --- | --- | --- |
| S1 | 删掉 `include/` 影子层：公开头唯一物理位置 | 已落地 |
| S2 | 修正聚合边界与依赖倒置 | 进行中（2a 已落地） |
| S3 | 削掉两个最大的文件（只做有明确接缝的） | 待做 |
| S4 | 命名收敛（在 S1/S2 之后） | 待做 |
| S5 | 计划目录收敛：已收口/被接手的线归档，只留仍有代码要改的线 | 已落地 |
| S6 | 2026-09-19 复查：死公开头（已落地）、`Utility/` 杂物抽屉（已落地）、`Panels` 命名（已落地）、`EditorSelection` 去重（待做）、Render3D→Physics | 进行中 |

### S5：计划目录收敛

目标不是"把 plan 目录压到 2–3 个"——那会逼着把还有开放 checkpoint 的线也塞进
archive，等于把工作藏起来。真正要修的是**读者无法区分"现在要改什么"和"当初
想过什么"**。做法：

- 归档已收口（checkpoint 全绿且结论已进 skill）、已被别的线接手、或已显式
  延后的线与 `./archive/` 合并；删掉活跃入口。
- 在 `./AGENTS.md` 里维护"当前活跃线"表与每次归档的理由表，让 `ls` 之外还有
  一份明确入口。
- 归档不删内容：历史命令、当时 baseline、被否决的方案都保留。

实测：活跃目录 25 → 12，归档 13 条；4 处 skill 引用与 1 处测试注释同步改到
archive 路径。剩余 12 条里有 9 条仍有开放 checkpoint，压不动。

### S1：公开头唯一物理位置

目标形态（已写入 skill，属强制规则）：

- 需要公开的头放在 `<module>/include/<Module>/...`；公开路径 = 相对 include 根的路径。
- 模块根不再保留同名副本；`include/` 下不得出现转发 stub。
- 一个物理头只有一个公开路径（禁止别名）。
- 私有头留在模块根/子目录，紧挨使用它的 `.cpp`。
- 公开头只能 include 公开头；若公开头需要某个私有头，就把该头提升为公开，
  而不是把它的目录也加进 include 根。
- include 字符串一律写公开路径，不写 `../`、不写裸文件名跨目录。

验收：

1. `include/` 下 0 个转发 stub。
2. 0 个物理头有多个公开路径。
3. 全部公开头位于其公开路径。
4. `add_headerfiles` 无失效模式。
5. PCH 指向新物理位置。
6. `xmake b ya-engine` / `ya-testing` / `ya-render-3d-test` / `ya-game-editor` /
   `ya-game-runtime` / `GUIWorkbench` / `HelloMaterial` / `GreedySnake` / `ya-runtime` 通过。
7. 搬迁保留 blame（见 `progress.md` 的两次提交形态）。

## 3. 后续（S2 起，未开始）

S2 的候选清单（每条独立可验收，动手前先确认消费面）：

- **S2a（已落地 2026-09-19）**：`ya-engine` 不再公开包含 `ya-game-runtime`。见 `progress.md`。
- **S2b（待做）**：拆 `ya-gui-framework`：它现在是 GUI + app shell + module system 的混合体，
  这正是"抽不出独立 GUI app"的根因。`Tooling/Workbench` 是 demo app，不属于 GUI 库。
- **S2c（待做）**：断 `ya-scene-core -> ya-gui-widgets`（SceneWidgetEntry）与
  `ya-render-3d -> ya-gui-compose`（`Render2DComposePass` 出现在
  `RenderDeviceState.cpp` / `RenderDeviceState.Frame.cpp` / `ViewCompose.cpp`；
  2026-09-19 起 `RenderFrameCoordinator.cpp` 已并入 `RenderDeviceState.Frame.cpp`）。
- ~~修 `ya-rhi-backend-common` 自依赖自身的笔误。~~ **2026-09-19 复查：此条已不存在**。
  `Engine/Source/Framework/RHI/Backend/xmake.lua` 里 `ya-rhi-backend-common` 只 deps `ya-rhi`，
  唯一引用它自己的是 `ya-rhi-vulkan`（合法的单向依赖）。原条目是过时假设。

## 4. 退出条件

- 读一条主链路时不再出现"同一概念四层转译、每层各叫一遍"。
- 目录里不存在同一个东西的两个入口。
- 搜索真相时 `.agent/plan` 不再贡献一半命中。

## 5. S6：2026-09-19 复查新增项（目录形状，不动主链路）

这次从目录形状复查，S1/S5 的成果都在（公开头 0 个转发 stub、0 个多路径别名；活跃计划 12 条）。
新发现六条，都属"读者找不到东西"或"职责倒置"，**都不需要新抽象**。按性价比排序：

### 6.1 死公开头（已落地 2026-09-19）

全仓按 basename 扫 `include/` 下的头，只有 4 个零引用：

```
Framework/Core/include/Core/Object.h                 21 行，整文件被注释掉，连 #pragma once 都没有
Framework/Core/include/Core/Math/ScreenUtil.h        21 行
Framework/Core/include/Core/Scripting/Lua/YaLua.h    22 行
Framework/Core/include/Core/Reflection/ReflectionHelper.h  41 行
```

`Object.h` 尤其典型：一个"曾经想写"的空壳留在公开 include 面上，`rg Object.h` 的命中全是噪声。

**处置已执行**：4 个头删除（`git rm`）。删除前逐一确认零引用（`Engine` / `Example` / `Script` /
`.agent` 四个根都扫过），且不在任何 `xmake.lua` 里被显式列出——`add_headerfiles("./include/**.h")`
是 glob，所以删除只影响安装清单。验证：`ya-foundation-core` / `ya-engine` / `ya-testing` /
`ya-game-editor` / `ya-game-runtime` / `GUIWorkbench` / `ya-render-3d-test` 全部 build ok，
`ya-render-3d-test` 175/175。

若 `ScreenUtil` 的世界→屏幕换算以后有需要，它属于 Render2D / 相机投影，不该以一个孤立头的形式回来。

### 6.2 `GameRuntime/Utility/` 是杂物抽屉，且藏了渲染主流程的一步（已落地 2026-09-19）

6 个文件彼此无关：`AppScreenshotCapture`（自动化）、`FPSCtrl`（计时）、`OffscreenJobRunner`（离屏
pump）、`RenderFrameExtractor`（**场景抽取：declareViews 与 prepareViews 之间的那一步**）、
`SceneCameraQuery`（场景查询）、`UiFontSettings`（字体配置）。

代价是**可发现性**：一个读者顺着 `tickRender` 追 `extractScenes` 会落到 `Utility/`——没人会去那里找
主流程。而 `Utility` 这个名字本身不描述任何职责，所以它还会继续收东西。

**处置已执行**：`Utility/` 与 `include/GameRuntime/Utility/` 两个目录已删除。按"读者会去哪找"分配：

| 文件 | 新位置 | 依据 |
| --- | --- | --- |
| `RenderFrameExtractor` | `Lifecycle/` | 它是 `declareViews` 与 `prepareViews` 之间的那一步，两个生产消费者（`GameRuntimeTickOrchestrator`、`HostSceneExtract`）都在 Lifecycle |
| `SceneCameraQuery` | `Lifecycle/` | `findPrimaryCamera` 被 tick 的 camera aspect 与 game view producer 使用，两者都在 Lifecycle |
| `FPSCtrl` | `Lifecycle/` | 帧节奏属于循环的计时（`iterate` 调 `update`，`AppLifecycle` 从配置设值） |
| `AppScreenshotCapture` | `Automation/` | 唯一生产消费者是自动化（tick 自动化 + 控制面） |
| `UiFontSettings` | `Settings/`（新） | 头文件自己声明它是"用户可见、持久化、跨 shell 共享的应用策略"，并预留更多 UI 设置（text scale / density）加入同一个 `ui` 配置文档 |
| `OffscreenJobRunner` | **删除** | 见下 |

**`OffscreenJobRunner` 为什么是删除而不是搬迁**：`queueOffscreenJob(App*, render, job)` 在整个生产代码里
**零调用方**——三个 .cpp（`AppAutomation` / `AppAutomationControlService` / `AppScreenshotCapture`）include
了它却从不调用，是历史残留的 include（已一并删除）。生产上真正在用的是
`AppAutomation::buildOffscreenJobQueueService(App&)`，而 `queueOffscreenJob(App*,...)` 的内部就是
"用 app 构造同一个 service 再委托"，即**重复了一个活着的 helper**。测试是它唯一的调用方，于是按
"找不到生产者一律删除"处置：`OffscreenAsyncTest` 的 8 处改为走 `buildOffscreenJobQueueService`
（`&app` 分支）与空 service（null-app 分支，RHI 对空 service 的拒绝与对 null renderer 的拒绝一致），
覆盖语义不变，且测的是产品真正走的接线。

**搬迁方式**：全部 `git mv`（保留 blame），include 用逐模式 `perl -pi` 精确替换而非行号 sed；改完用
`rg 'GameRuntime/Utility' Engine Example` 归零作为验收。`GameRuntime/xmake.lua` 用
`add_files("**.cpp")` 与 `add_headerfiles("./include/**.h")` 两个 glob，所以新目录自动纳入，无需改构建。

验证：`ya-testing` / `ya-game-runtime` / `ya-game-editor` / `ya-engine` / `ya-runtime` /
`GUIWorkbench` / `ya-render-3d-test` 全部 build ok；受影响用例
（`OffscreenAsyncTest` / `AppScreenshotCaptureTest` / `UiFontSettingsTest` /
`HostSceneExtractTest` / `EditorSettingsDialogTest`）31/31；`ya-render-3d-test` 175/175；
渲染/编辑器滤镜 304 passed / 3 failed（与基线同 3 个 pre-existing）；两张 smoke 截图逐字节相同；
编辑器 smoke 六步全过。

### 6.3 `GameEditor/Panels/` 的名字与 `UI/Tabs/` 冲突（已落地 2026-09-19）

三个 `*Panel` 都不是 retained 控件，而是 **tab 的领域状态**：

| 文件 | 行数 | 实际是什么 | 谁在用 |
| --- | --- | --- | --- |
| `UIDesignerPanel` | 654 | UI designer 的文档 / 选择 / 调色板模型 | `EditorUIDesignerTab`、`EditorUIDesignerTools`、`EditorViewportCompositor` |
| `SceneHierarchyPanel` | 199 | Hierarchy 的选择状态 | `EditorHierarchyTab` 一侧 |
| `AssetInspectorPanel` | 27 | 一个 `inspectedPath` 字符串 | `EditorAssetInspectorTab` |

于是同一对概念有两个名字：**`Panel` = 模型，`Tab` = 视图**。读者看到 `Panels/` 与 `UI/Tabs/` 无法
判断哪个才是面板。`gui-editor-structure` 的 C1/C2 已经登记过"三份 domain panel 尚未改名"，本项把它落掉。

**处置已执行**：`Panels/` 与 `include/GameEditor/Panels/` 两个目录删除。三个文件按"它是什么"处理：

| 原名 | 处置 | 依据 |
| --- | --- | --- |
| `SceneHierarchyPanel` | → `EditorSelection`（模块根） | 它是**编辑器的实体选择状态**（成员 `_selections` / `_primarySelection` / `_rangeAnchor`），viewport picking、gizmo、ops、Interaction 都在用它，不专属 hierarchy tab。头注释原本就写"Entity selection bus for viewport pick" |
| `UIDesignerPanel` | → `EditorUIDesignerSession`（模块根） | 它是 UI designer 的**会话**：一个打开的文档 + 独立 preview 树 + 选择 + 拖放/缩放操作。与既有的 `EditorPlaySession` 同类；改名后 `Session`（状态）对 `EditorUIDesignerTab`（视图）不再含混 |
| `AssetInspectorPanel` | **删除**，状态并进 `EditorLayer` | 它只有 27 行，是 `_inspectedPath` 一个字符串的包装；`_owner` 从未被使用（唯一 setter 里 `(void)_owner`），`isVisible()` / `clear()` 零调用方，`_bVisible` 只是 `!path.empty()` 的副本且唯一的读点也在被删除的 `isVisible()` 里。真正的状态是"Content Browser 写、Asset Inspector tab 读"的一条路径，直接放 `EditorLayer::_inspectedAssetPath`（+ `inspectAsset` setter / `inspectedAssetPath` getter）即可 |

**修正了原计划的一处判断**：原计划写"按'它们是什么'命名并放到 tab 视图旁边"。实测 `EditorSelection` 的
消费者几乎全在 `EditorLayer` 自己的 TU（Interaction / Startup / ViewportAuthoring），并不专属某个 tab；
所以落点是**模块根**（与 `EditorPlaySession` / `EditorViewProducer` / `EditorRuntimeSettings` 并列的
"编辑器会话级对象"），不是 `UI/Tabs/`。

**顺带发现、本轮不做的**：`EditorSelection` 的选择状态与 `EditorLayer` 持有的副本（`_selections` /
`_selectionGeneration` / `getSelectedEntity()`）是同一事实的两份存储，靠 `notifyOwnerSelection()` →
`setSelections()` 手动同步——与 `viewportRect` 同类。合并成单一所有者会改动跨文件的选中语义，需要
自己的验证切片，登记在下面而不是塞进这次改名。

**搬迁方式**同 6.2（`git mv` + 逐模式 perl），验收：`rg 'GameEditor/Panels/' Engine` 归零。
验证：`ya-game-editor` / `ya-testing` / `ya-game-runtime` / `ya-engine` / `ya-runtime` /
`GUIWorkbench` / `ya-render-3d-test` 全部 build ok；改名后的 `EditorUIDesignerSessionTest` 6/6；
渲染/编辑器滤镜 310 passed / 3 failed（与基线同 3 个）；两张 smoke 截图逐字节相同（runtime
`1c666897…`、editor `5b8f5dd8…`）；编辑器 smoke 六步全过。

### 6.3b 待做：`EditorSelection` 与 `EditorLayer` 的选择状态去重

今天 `EditorSelection::_selections` 与 `EditorLayer::_selections` 并存，由
`notifyOwnerSelection()` 推一次 `setSelections()` 保持同步。两个写者对同一事实，读者分散在两处。
目标：一个所有者，另一侧只读。验收：选中相关成员只存在一份；多选/框选/删除/复制/上下文菜单的
行为与 `EditorHierarchyOps` 相关用例不变。

### 6.4 `ya-render-3d -> ya-physics` 只为画调试线

**已落地 2026-09-19**：`PhysicsDebugDraw.{h,cpp}` 从 `Render3D/Debug/` 移到
`Adapters/Debug/`（公开路径 `Render/Adapters/Debug/PhysicsDebugDraw.h`，导出宏改
`YA_RENDER_ECS_ADAPTERS_API`），`ya-render-3d` 去掉 `ya-physics`，改由
`ya-render-ecs-adapters` 持有——它本来就是"ECS 组件 ↔ 渲染"的桥。验证：`rg 'Physics/|PhysicsBody|drawPhysicsCollision'
Engine/Source/Framework/Render/Render3D` 归零，render-3d 的 xmake 里只剩注释提到 physics；
7 个目标 build ok；`ya-render-3d-test` 175/175；滤镜 558 passed / 3 failed（同基线 3 个）；
两张 smoke 截图逐字节相同。

这条修的不只是"少一个 dep"：之前`渲染器`知道 `PhysicsBodyShape` 枚举与 `kDefaultSphereRadius` /
`kDefaultBoxHalfExtent`，头注释还写着"与 PhysicsSystem 的创建规则保持同步"——一条靠人维护的跨模块
耦合，现在它落在桥接层，语义与位置一致。

`add_deps("ya-physics", ...)` 是 implementation-only，唯一消费者是 `Render3D/Debug/PhysicsDebugDraw.cpp`：

```cpp
#include "Physics/PhysicsBodyComponent.h"
... scene.getRegistry().view<TransformComponent, PhysicsBodyComponent>().each()
... bodyComponent._shape == PhysicsBodyShape::Sphere
... PhysicsBodyComponent::kDefaultSphereRadius / kDefaultBoxHalfExtent
```

渲染器因此知道物理组件的形状枚举与默认半径/半长（注释里还写着"与 PhysicsSystem 的创建规则保持同步"——
这是一条靠人维护的跨模块耦合）。处置：按 `Render/Adapters` 已有模式，让物理侧发布调试线段、或在
adapter 里画，renderer 只收线。**注意**：这与 S2 的 `ya-render-3d -> ya-gui-compose` 是同类问题，
可以合到同一刀。

### 6.5 S2 里对 AppKernel 的判断需要修正（不删）

S2 候选清单里有"拆 `ya-gui-framework`：它现在是 GUI + app shell + module system 的混合体"。
2026-09-19 复查后确认**拆聚合目标是对的，删 AppKernel 是错的**：

```
GUI/Host/xmake.lua      add_deps(... "ya-app-kernel", "ya-app-control", { public = true })
GUI/xmake.lua           add_deps(... "ya-app-kernel", "ya-app-control", "ya-hierarchy", ...)
GUIWindowHost / GUIApp / GUIHeadlessHost  都 implements IAppLoopDelegate，各自在 run() 里构造 AppKernel（4 行）
```

AppKernel 持有的是**共享控制面**而不是应用基类：单实例 `OsProcessLock`、`AppAutomationRunController`
（exit-after-frame / 墙钟上限 / 远端退出）。这两件事各有 memory（`control_instance_lifecycle`、
`app_teardown_order_and_instance_lock`）记录过坑，删掉 AppKernel 会把它们复制到 3 条产品线上。
"GUI 被锁死"的实际成本只是 5 个虚函数；真正的噪声是 `ya-gui-framework` 这个聚合目标把
AppKernel/AppControl/Hierarchy 一起公开出去。所以这一刀只拆聚合，保留 Kernel。

### 6.6 S2 现有条目状态

S2 的四条候选里，`ya-scene-core -> ya-gui-widgets` 已量化：
`Scene/Core/Scene.h` 按值持有 `std::vector<SceneWidgetEntry>`，而 `SceneWidgetEntry.h` include 了
`GUI/Layout/UICanvasLayout.h` 与 `GUI/Widgets/UIDocument.h`。`Scene.h` 有 **68** 个 includer，
即每个读 Scene 的人都顺带拉进 GUI layout/widgets 头。`SceneWidgetEntry`（106 + 437 行）是 Game UI
的授权数据住在 Scene 模块里。这一条**没变，仍待做**，但现在有了可验收的量化口径：
`rg 'GUI/(Widgets|Layout)' Engine/Source/Framework/Scene` 归零，且 `Scene.h` 的 includer 不再传递
GUI 头。
