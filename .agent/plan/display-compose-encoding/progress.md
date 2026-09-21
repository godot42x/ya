# Progress

## 2026-09-21 — F1 落地

提交拆分（已完成）：

F1 的代码改动最初是在本目录建成之前、与另一个写入者的 viewport->view 重命名同时进行的，
提交时被并入对方的提交 `3a4144d4`。该提交已按内容拆分：

- `57c55862 [render/view] view is the render vocabulary`（原 3a4144d4 去掉 F1，
  只剩纯重命名；691+/691-，可独立编译）
- `4a78b5fd [render] display compose copies the view image instead of grading it again`
  （F1 本身，16 文件 95+/36-）
- `f1ebdf4c [render] gate that display compose copies instead of grading`（门禁 + 本目录）

拆分判据是内容守恒：`git diff 4a78b5fd 3a4144d4` 为空，即「重命名 + F1」逐字节等于原提交；
整个 main 与改写前的 `backup/main-pre-f1-split` 也是 `git diff` 为空。
改写前的分支留在 `backup/main-pre-f1-split`，确认无误后可删。

改动清单（现在都在 HEAD 里）：

- PostProcessingState.h：+ passThrough() / withoutGrading()。
- PostProcessingStage.h/.cpp：bEnabled -> bGradingEnabled（setGradingEnabled /
  isGradingEnabled）；appendFinalizeGraphPasses 无条件运行；grading 关掉时用
  withoutGrading()；appendBloomGraphPasses 关掉 grading 时返回 input 而不是无效 handle。
- ForwardRenderPipeline.cpp / DeferredRenderPipeline.cpp：display image 恒为 finalize 输出。
- RenderDeviceState.Frame.cpp：getViewDisplayImageFormat() 恒为 postprocess 格式。
- IRenderPipeline.h / Forward+Deferred pipeline / RenderDeviceState / AppRenderServices：
  isPostprocessingEnabled -> isGradingEnabled（名字要诚实：pass 仍然每帧跑）。
- AppAutomation.cpp：stage.setEnabled -> setGradingEnabled。
- PresentationGraphService.h/.cpp：删掉 _presentationPostProcessState，display compose
  用文件内常量 kDisplayComposeState = passThrough()。

验证（拆分后在新 main 上重放）：

- 门禁 Script/automation/render/run_display_compose_parity.py：
  viewport = c775245ae636f15b41da8485319a2267，presentation = 同一个值，PASS。
  这个值同时等于 F1 之前的 viewport 基线，即 View 路径逐字节未被扰动。
- xmake r ya-render-3d-test：175/175。
- 编辑器冒烟：exit 0，无 error/assert。窗口截图 md5 由 ffc3310a 变为 f78bb162，
  但同一个新值连续两次运行一致，且 `Engine/Saved/Config/Editor.json` 里带
  `dockLayout`（编辑器自己运行的产物，非跟踪文件）——这是本目录已知的 dock 布局漂移，
  不是代码回归：树内容与改写前逐字节相同，运行时 viewport/presentation 也仍是基线值。
  没有做配对运行，所以这里只声明「与已知漂移一致」，不声明「截图未变」。
- ya-testing 宽滤镜：694 tests / 688 passed。5 个是既有基线（EditorPropertyGraph 两例、
  WidgetLayoutTest.FloatingWindowResizeHandlesLiveOnOverlaySlots、
  ScriptApiLibraryFixture.GameUIWidgetLifecycleThroughRegistry、
  GameUIHostTest.BuildSnapshotComposesMountedWidgets）。

未归属的观察（记在这里，不掩盖）：

- ya-testing 宽滤镜下 RenderGraphCoreTest.ResourceRegistryUsesProvidedImportedImageViewAndRetainsOwner
  曾失败一次：registry.clear() 之后一个 retained owner 的 weak_ptr 没有 expire。
  它单独跑通过、在 103 个用例的组合里通过、在宽滤镜里失败过、随后连跑 3 次又全部通过。
  非确定性。测试文件在 HEAD 未改动；这块代码（RenderGraph 资源保留）与本切片无交集。
  怀疑是某个前置 suite 把 GPU 资源留在了进程级缓存里，属独立问题，需要单独查。

## 2026-09-22 — F2 落地

改动（一个 `[render]` 提交）：

- `PresentFrameInput::bCopyViewDisplayImage`（默认 true）：窗口底板是宿主声明，
  不由渲染器从 View 的 compose 结构推断。
- `PresentationGraphService::recordDisplayCompose(bCopyViewDisplayImage, …)`：false 时
  不拷贝、也不调用 provider；pass 照常跑，host 的 chrome 照常在同一个 pass 内录制。
- 谁铺 surface 谁回答：`IRuntimeModule::fillsPrimarySurface()`（查询，默认 false），
  `App::presentsViewDisplayImage()` 遍历模块，`GameRuntimeTickOrchestrator::recordFrame`
  把答案填进 plan；`EditorModule` 就是那个答 true 的宿主。
- 第一版判据写成 `app.isRuntimeMode()`，**是错的**：编辑器工具栏的 Play 走
  `App::startRuntime()`，属于「编辑器内 runtime」，窗口仍是编辑器的、chrome 照样铺满。
  用 AppState 判会让 Play 期间又退回一次全屏白拷。改成模块查询后，编辑器在任何状态下
  都答 false；这条已经由探针在真实 Play 会话里验证（见下）。
- 命名修正：`_presentationPostProcessor` -> `_displayImageCopy`、
  `_presentationToneMap` -> `_displayImageCopyBindings`、池 label
  `Presentation_ToneMap_DSP` -> `Presentation_DisplayCopy_DSP`（passThrough 下它就是一次
  采样拷贝，ToneMap 的命名会让人以为这里还在做 tonemap）。

证据：

- 探针（改完即撤，未留在代码里）逐帧打印 copy/source，并同时打印当时的 AppState 与判据：
  - 独立 runtime：20 帧全部 `copy=1 source=1`。
  - 编辑器 Stopped：全部 `runtime_mode=0 presents_view=0 copy=0 source=0`。
  - 编辑器内 Play：用 automation control 口（`Script/ya.py control start` +
    原始 RPC `set_app_state {"state":"runtime"}`）真实进入 runtime 状态，得到
    `runtime_mode=1 presents_view=0`，`copy=0` 全程成立；整场 2839 帧没有一帧 `copy=1`。
- 独立 runtime 门禁仍 PASS：viewport = presentation = `c775245ae636f15b41da8485319a2267`，
  与 F1 之前基线逐字节相同 → runtime 路径未被扰动。
- 编辑器 presentation 截图，排除 HUD 计时区域后：改动前后逐字节相同，改动后两次运行也
  逐字节相同（各自 0 px 差异）。也就是说被删掉的全屏拷贝确实在窗口上没露出过任何像素
  ——编辑器 chrome 本来就不透明铺满。这是「这次拷贝纯浪费」的直接证据，也是「无视觉回归」
  的证据。
- `xmake r ya-render-3d-test`：175/175。
- 宽滤镜：696 跑 / 679 过 / 6 失败，失败集合与既有基线一致（5 个已知 + RenderGraphCoreTest
  那个 flake）。新增 `AppLifecycleTest.TheSurfaceBackdropIsWhatTheLoadedModulesSayItIs` 锁住
  「没有模块铺 surface → 窗口显示 View；有模块铺 → 不拷；模块不再铺 → 又拷」这条分支。

本轮新发现（与 F2 无关，但会影响别人验证）：

- `WidgetTreeTest.SystemLayersCannotBeDetached` 会以 SIGTRAP 打死整个测试进程。该用例故意
  detach 系统 layer，而 `YA_CORE_ASSERT` 是 `log + PLATFORM_BREAK()`。HEAD 上 WidgetTree.cpp
  与该用例都未被修改（`git diff --stat HEAD` 为空），所以这是既有问题，不是本次改动引入。
  本轮宽滤镜用 `-WidgetTreeTest.SystemLayersCannotBeDetached` 排除它。要不要把「detach 系统
  layer」从 trap 改成返回 refused（那样用例才有意义）属于 GUI 侧的决定。
- 编辑器截图漂移来源确认：Frame Inspector HUD 的实时计时文字（默认开启）。比对区域
  x 1140..1240 / y 735..776，其余逐字节稳定。
- 未归属：第一次 `control start` 拉起的编辑器实例在首帧前后以 SIGBUS
  （`KERN_PROTECTION_FAILURE`，崩溃点在 `App::presentsViewDisplayImage`，出错地址落在
  dyld shared cache 里）死掉。同一条命令随后重复多次不复现，进入 Play 后连续 2800+ 帧也
  不复现；该函数只是只读遍历。倾向于构建/映射层面的偶发（运行中的进程其 dylib 被重链），
  但没查实，所以留在这里。

## 未完成

- 待定语义：bEnableGammaCorrection 关掉时会写出线性图像，编辑器路径今天就已经这样。
  F1 让窗口路径与之保持一致，但这个开关本身该不该存在没有决定。
- 编辑器 HUD 没有关掉的可编程入口（`applyGuiFrameInspectorSpec` 只被 GUI host 的 config
  调用），所以编辑器截图目前只能靠「排除 HUD 区域」来做回归门禁。若要让编辑器截图也
  变成可逐字节比对的门禁，需要给编辑器一条关掉 HUD 的通道（CLI 或 config）。
- `WidgetTreeTest.SystemLayersCannotBeDetached` 的处置（改用例为 death test，或让
  detach 系统 layer 返回 refused）留在 GUI 侧。
