# Progress

## 2026-09-21 — F1 落地

代码位置（注意归属）：

F1 的代码改动是在本目录建成之前、与另一个写入者的 viewport->view 重命名同时进行的，
提交时被并入对方的提交 `3a4144d4 [render/view] view is the render vocabulary`。
本轮 commit 只带门禁脚本与计划工件。若需要把它独立成自己的提交，需要改写 3a4144d4，
那是历史重写，先问用户。

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

验证（在 HEAD 内容上执行，因为改动已被并入）：

- 门禁 Script/automation/render/run_display_compose_parity.py：
  viewport = c775245ae636f15b41da8485319a2267，presentation = 同一个值，PASS。
  这个值同时等于 F1 之前的 viewport 基线，即 View 路径逐字节未被扰动。
- xmake r ya-render-3d-test：175/175。
- 编辑器冒烟：exit 0，无 error/assert，窗口截图 md5 ffc3310a7bf11cd306ff8acfeab40ca5，
  与 F1 之前逐字节相同。
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

## 未完成

- F2：编辑器在 present 里多铺一次全屏世界图（见 plan.md）。需要先实测 pass 计数确认。
- 待定语义：bEnableGammaCorrection 关掉时会写出线性图像，编辑器路径今天就已经这样。
  F1 让窗口路径与之保持一致，但这个开关本身该不该存在没有决定。

