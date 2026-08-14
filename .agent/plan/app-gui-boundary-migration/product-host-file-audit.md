# Phase A2 — Product/Host File-Level Consumer Audit

> 更新时间：2026-08-14
> 作用：把 Engine/Source/Product/Host 下的文件拆成明确几类：app-form shell、共享能力消费者 façade、branch-local GUI/game adapter、compat only，以及应删除的噪声文件。

## 1. 审计结论先说

Product/Host 不是下一轮可以整体 rename 的目录；它本质上是几类东西混放：

1. App 这类 game runtime / runtime editor 的厚壳；
2. 对共享能力的消费侧 façade（render / scene / task / screenshot）；
3. branch-local GUI adapter（Game UI、ImGui backend、editor/runtime glue）；
4. 还没清掉的死文件与 compat 头。

因此 Phase A 的正确动作不是“给 Product/Host 起个新名字”，而是：

- Batch 1 只允许它改 include，接回新的 App/Kernel、App/Control、GUI/Host；
- Batch 2 再按下表把真正的 app-form shell 留下，把 façade、adapter、dead stub 拆散归位；
- 任何想把整棵 Product/Host 平移到 Game、Applications 或 Framework 的做法都应停止。

## 2. 判定规则

本审计只用下面五类结论：

| 分类 | 含义 | 迁移动作 |
|---|---|---|
| app-form shell root | game runtime / runtime editor 的组合壳 | 保留到 app-form 分支；不下沉到共享能力 |
| consumer façade | 只是共享能力 owner 的消费侧薄封装 | 不升格为共享 owner；跟随 app-form shell 或回到对应 branch-local leaf |
| branch-local adapter | 只服务某类 app-form GUI / game / editor 适配 | 留在对应 app-form 分支，不回灌 GUI/Runtime 或 App |
| compat only | 仅旧 include/target 过渡壳 | 在上游迁移完成后删除 |
| dead / delete candidate | 不再形成稳定职责 | 直接删，或并入真正 owner；不要迁移保留 |

补充规则：

- include/Host 下的普通 mirror header 默认继承对应源文件 owner，不再逐个重复列；
- 只有 include/Host/NativeWindowManager.h 这类纯 alias 头，单独视为 compat only；
- “当前很多地方 include 它” 只能证明它重要，不能证明它是共享 owner。

## 3. 文件级审计表

### 3.1 App 壳与内部状态

| 文件 | 证据 | 分类 | 未来 owner / 分支 | Batch 动作 |
|---|---|---|---|---|
| App.h/.cpp | 持有 NativeWindowManager、GameUIHost、AppRenderState、AppSceneServices、TaskManager；applyProjectDescriptor 直接 mount game mounts；被 Example/HelloMaterial、Product/Editor、YARuntime 直接 include | app-form shell root | Applications/GameRuntime 与 runtime/editor 共享壳的根入口（命名待后续拍板） | Batch 1 不移动；Batch 2 只允许在明确 app-form 归属后归位 |
| AppContext.h | 仅 struct AppContext { App* app; }；没有独立能力 | dead / delete candidate | 删除或并回真实 owner | 不迁移；Batch 2 前先确认是否仍有消费者 |
| AppEvent.h | 只有模板 LazyStatic 和 TODO 注释；无明确 Host owner | dead / delete candidate | 删除或并回真正 owner | 不迁移；优先作为减法候选 |
| AppRenderFrameState.h | 只是 viewport/camera/frame 的数据结构 | consumer façade | 跟随 app-form render shell；不单独升为共享 owner | Batch 2 与 AppRenderState、AppRenderServices 一起审 |
| AppRenderState.h | 聚合 RenderRuntime、ShadowSettings、frame data；是壳内 render state owner | consumer façade | 跟随 app-form render shell | Batch 2 视 render 壳拆分一起归位 |

### 3.2 对共享能力的消费侧 façade

| 文件 | 证据 | 分类 | 未来 owner / 分支 | Batch 动作 |
|---|---|---|---|---|
| AppRenderServices.h/.cpp | 只是包一层 AppRenderState 访问 RenderRuntime、ShadowSettings、overlay/frame state | consumer façade | 跟随 app-form shell；不是 Render/Runtime owner | Batch 1 不动；Batch 2 与 AppRenderState 同步看是否保留 façade |
| AppSceneServices.h/.cpp | 只是通过 App、AppLifecycle、AppFrameLoop 操作 scene manager / active scene | consumer façade | 跟随 app-form shell；不是 Scene owner | Batch 1 不动；Batch 2 再判断是否拆成更薄 scene shell |
| AppTaskManager.h | TaskManager 继承 IOffscreenTaskScheduler，直接持有 frame/offscreen task 队列；实现的是消费侧 scheduler | consumer façade | 跟随 app-form shell；不单立成共享 App owner | Batch 1 仅改 include；Batch 2 再决定是否需要抽成 branch-local task shell |
| Utility/OffscreenJobRunner.h/.cpp | 只是把 offscreen job 排到 App/task manager；消费 OffscreenJobQueueService | consumer façade | 跟随 app-form shell 或 render app-form leaf | Batch 1 不动 |
| Utility/AppScreenshotCapture.h/.cpp | 基于 render graph / offscreen queue 的 screenshot helper；不拥有通用 capture 协议 | consumer façade | 跟随 app-form shell 的 render automation leaf | Batch 1 不动 |
| Utility/RenderFrameExtractor.h/.cpp | 从 scene + camera + shadow settings 提取 RenderFrameData；是 runtime 壳的抽取工具 | consumer façade | 跟随 game/editor runtime shell；不是共享 Render/Runtime owner | Batch 2 与 render 壳一起归位 |

### 3.3 生命周期、路由、运行壳

| 文件 | 证据 | 分类 | 未来 owner / 分支 | Batch 动作 |
|---|---|---|---|---|
| Lifecycle/AppLifecycle.h/.cpp | 负责 project 场景启动、module attach、render runtime 初始化、script/physics/file watcher/font/material 等 runtime 组合 | app-form shell root | app-form runtime shell | Batch 1 只改新 include；不把它错移到 App/Kernel |
| Lifecycle/AppFrameLoop.h/.cpp | 自己不再 owning while loop，但仍是 product frame work：SDL/native event pump、render tick、automation/screenshot/fps/viewport sync | app-form shell root | app-form runtime shell | Batch 1 只适配 AppKernel 与 GUI/Host 新路径 |
| Lifecycle/AppEventRouter.h/.cpp | 依赖 NativeWindowManager、主窗口 identity、focus/minimize、last mouse pos；是 product shell 的路由策略 | consumer façade | 跟随 app-form shell；不是共享 App/Kernel route owner | Batch 1 不动 |
| InputRouter.h/.cpp | 依赖 App、GameUIHost、SDL capture / cursor；当前是 runtime/editor 输入节点链路 | consumer façade | 暂留 app-form shell；只有出现第二个非 game consumer 时再评估共享化 | Batch 1 不动 |
| Utility/SDLMisc.h | SDL 到 engine event 的本地转换 helper；服务当前壳的 native event pump | consumer façade | 跟随 windowed app-form shell；不回到 App/Kernel | Batch 1 不动 |
| Utility/FPSCtrl.h/.cpp | runtime 壳的帧率限制器；SDL delay 细节证明它是 product loop 附属 | consumer façade | 跟随 app-form shell | Batch 1 不动 |

### 3.4 automation / control 的产品扩展层

| 文件 | 证据 | 分类 | 未来 owner / 分支 | Batch 动作 |
|---|---|---|---|---|
| Lifecycle/AppAutomation.h/.cpp | 读取 automation config、截图、RenderDoc、scene stable、viewport resize、pipeline switch；强绑定 runtime/product 行为 | app-form shell root | app-form runtime/editor 的 control extension | Batch 1 仅切到新的 App/Control include；不下沉到共享 App/Control |
| Automation/AppAutomationControlService.h/.cpp | 直接操作 scene/entity/render/editor camera/js eval/screenshot；明显是产品扩展 RPC，而不是共享 control plane 基底 | app-form shell root | app-form runtime/editor control extension | Batch 1 仅改 include |
| Automation/EditorAutomationControl.h | 只定义 editor extension interface（authoring scene、editor camera） | branch-local adapter | Applications/GameEditor 侧扩展接口 | Batch 1 不动；Batch 2 归到 editor branch |
| Bootstrap/AutomationSceneBootstrapModule.h/.cpp | 只为 runtime 壳注入默认 automation scene path | branch-local adapter | app-form runtime shell | Batch 1 不动 |

### 3.5 GUI、Game UI、ImGui 适配层

| 文件 | 证据 | 分类 | 未来 owner / 分支 | Batch 动作 |
|---|---|---|---|---|
| GUI/GameUI/GameUIHost.h/.cpp | 注释与实现都说明它是 game presentation area 的 live WidgetTree adapter；场景驱动 UI，不是 generic GUI app host | branch-local adapter | Applications/GameRuntime 下的 Game UI adapter | Batch 1 只修 GUI/Host include；绝不迁到共享 GUI/Host |
| GUI/GameUI/IGameUIController.h | 只服务 GameUIHost 的策略接口 | branch-local adapter | 跟随 Game UI adapter | Batch 1 不动 |
| GUI/GameUI/DefaultGameUIController.h/.cpp | auto-mount scene widget entry / world widget；明显是 game runtime 语义 | branch-local adapter | 跟随 Game UI adapter | Batch 1 不动 |
| GUI/GameUI/UIDocumentResolver.h/.cpp | 统一 yaui resolve/cache 规则，当前被 runtime 与 UIDesigner 共享 | branch-local adapter（带后续升级潜力） | 短期跟随 Game UI / editor UI 壳；若以后证明脱离 scene/runtime 仍独立，再单独抽回 GUI tooling | Batch 1 不动；Batch 2 复查是否值得脱壳 |
| GUI/GuiBackend.h | 只是 host-side GUI backend 接口，当前唯一实现是 ImGui | branch-local adapter | editor/runtime 壳的 GUI backend adapter，而不是共享 widget API | Batch 1 不动 |
| GUI/GuiSystem.h/.cpp | backend install/begin/end/submit/texture bridge；本质是 ImGui host shell 的 facade | branch-local adapter | 优先归 Applications/GameEditor 或共享 editor/runtime shell；不迁到 GUI/Runtime | Batch 2 再按真实消费者拆 |
| GUI/ImGui/ImGuiSystem.h/.cpp | 直接依赖 ImGui、SDL、Vulkan backend、ImGuizmo | branch-local adapter | editor/runtime 壳的 ImGui adapter | Batch 1 不动 |
| GUI/ImGui/Backend/ImGuiManager.Backend.Vulkan.cpp | Vulkan backend 细节，只服务 ImGuiSystem | branch-local adapter | 跟随 ImGui adapter | Batch 1 不动 |
| GUI/ImGui/ImageCache/ImGuiImageCache.cpp | ImGui texture bridge缓存 | branch-local adapter | 跟随 ImGui adapter | Batch 1 不动 |

### 3.6 脚本、profiling、杂项入口

| 文件 | 证据 | 分类 | 未来 owner / 分支 | Batch 动作 |
|---|---|---|---|---|
| Profiling.cpp | 只是 app-form shell 的 profiling 入口 glue | consumer façade | 跟随 app-form shell | Batch 1 不动 |
| ScriptApiAsset.cpp | product-host 侧 script API 注册 glue | consumer façade | 跟随 app-form shell / scripting glue | Batch 2 与 scripting shell 一起看 |
| ScriptApiCore.cpp | 同上 | consumer façade | 跟随 app-form shell / scripting glue | Batch 2 再审 |
| AppOptions.h/.cpp | 混合 app desc、automation、renderdoc、postprocess、CLI 参数；是壳入口配置面 | app-form shell root | app-form shell config surface；后续拆 shared control options vs product options | Batch 1 只切新 include；Batch 2 再拆配置面 |

### 3.7 明确的 compat / dead 噪声

| 文件 | 证据 | 分类 | 未来 owner / 分支 | Batch 动作 |
|---|---|---|---|---|
| include/Host/NativeWindowManager.h | 只有 include AppRuntime/NativeWindowManager.h，本身没有实现 | compat only | 旧 Host 路径兼容头 | Batch 1 可继续保留；当消费者切到 GUI/Host/NativeWindowManager.h 后删除 |
| Window/WindowsDialogWindow.h/.cpp | 整个文件被注释掉，没有活实现 | dead / delete candidate | 删除 | 不迁移 |
| Network/NetDriver.h | 空文件 | dead / delete candidate | 删除 | 不迁移 |
| Switcher.h | 空壳类型 CoreSwitch，无职责 | dead / delete candidate | 删除 | 不迁移 |
| Utility/ClLIParams.h | 无命名空间、无 owner 边界的裸 helper；当前只是旧 CLI 粘连残留 | dead / delete candidate 或并回真正 CLI/control owner | 后续应并到共享 control/CLI owner，而不是继续留在 Host | 不迁移 |
| Network/TODO.module.file | 明确是历史占位 | dead / delete candidate | 删除 | 不迁移 |

## 4. 直接影响到 Batch 1 的结论

1. Product/Host 在 Batch 1 里不做物理迁移。
2. 允许的唯一改动是：
   - 接到新的 App/Kernel、App/Control 公开头；
   - 接到新的 GUI/Host 公开头；
   - 逐步移除 include/Host/NativeWindowManager.h 这类纯 alias 依赖。
3. 不允许把 AppLifecycle、AppFrameLoop、GameUIHost、GuiSystem 误下沉成共享 owner。

## 5. 对 Batch 2 的硬约束

1. 先做减法：删 dead / compat 噪声，再谈 rename。
2. App、AppLifecycle、AppFrameLoop 应先作为一个 app-form shell 组保留，不要边拆边试图抽象出第二套共享 loop。
3. GameUIHost 与 ImGui 适配层不共享 owner：一个是 game presentation adapter，一个是 editor/runtime shell adapter。
4. UIDocumentResolver 先按 branch-local adapter 处理，只有当它脱离 scene/runtime/editor 都仍成立时，才允许单独抽回 GUI/tooling 共享层。
5. AppOptions 后续要拆“共享 control 选项”与“产品入口选项”，但这不是 Batch 1 任务。

## 6. 收口判断

完成这份审计后，Product/Host 的唯一正确口径是：

- 它不是共享能力 owner；
- 它不是未来要保留的顶层目录语义；
- 它只是 app-form runtime/editor 壳与若干 adapter/façade 的历史混放点；
- 真正需要迁移的是其中的子对象，而不是这整个名字本身。
