# 未初始化的 Rect2D 变成 View 尺寸：编辑器首帧崩溃（exit 255）

> 2026-09-18，做 render-view-family 的 4d-3b（作者视口 rect 由声明方给出）时暴露。
> 崩溃本身是老坑（未初始化几何），把它变成崩溃的是新语义（声明方直接给出 rect）。

## 现象

`run`（游戏）跑完紧接着 `run-editor`：编辑器以 **exit 255** 退出，日志在启动中途被截断
（最后一行常是某条 pipeline 创建），控制台没有断言文本。单独跑编辑器则正常；间隔久一点、
或换个顺序也常常正常。前 3 次连续复现全是「游戏→编辑器」，随后 8 次同样序列全绿（修复后）。

崩溃报告：`~/Library/Logs/DiagnosticReports/ya-runtime-*.ips`，`EXC_BREAKPOINT / SIGTRAP`，
栈是 `RenderGraph::createTexture` ← `createPersistentTexture` ← `SSAOStage::appendGraphPass`
← `appendSSAO` ← `DeferredFrameGraphOrchestrator::build` ← `recordFamily` ← `tickRender`。

## 根因链

1. `Rect2D` 之前没有默认成员初始化器，而 glm 默认构造是平凡的（没有 `GLM_FORCE_CTOR_INIT`），
   所以 `Rect2D viewportRect;` 这种成员是**未初始化内存**。`EditorLayer` 的
   `viewportRect` / `_viewportMouseRect` / `_viewportBounds[2]` 都在此列（仓库里同类
   `Rect2D x;` 共 13 处）。
2. 未初始化浮点常是**非规格化小数**（如位模式 `0x00000096`、`0x00000001`），
   `extent.x > 0.0f` 判为真，于是「还没布局的面板」看起来有尺寸。
3. 4d-3b 让编辑器**每帧直接声明**这个 rect（`EditorLayer::getViewportRect()` →
   `SceneViewDesc.viewportRect`）。`SceneRenderScheduler::seal()` 用
   `Extent2D::fromVec2(extent)` 截断成整数 → 0×0。
4. 0×0 的 View extent 进到 SSAO 的 persistent texture → `RenderGraph::createTexture` 的
   `extent must be non-zero` 断言 → `PLATFORM_BREAK()`。

旧代码（4d-2 及之前）把面板 rect 经 pending-resize 推给 device，只在「尺寸变化且鼠标未捕获」
时发生，所以同一根因更隐蔽；4d-3b 把声明提到每帧，垃圾值就每帧进图。

## 处置（三处，缺一不可）

- `Rect2D` 成员默认初始化：类型本身不该能是垃圾。一处修好覆盖全部 `Rect2D x;`。
- `EditorLayer::describesPixels()`：rect 必须**有限且至少一整个像素**；
  `notifyViewportWidgetRect` 与 `getViewportRect` 都用它，未布局/折叠的面板回落到编辑器默认尺寸，
  而不是声明一个会截断成 0 的 View。
- `SceneRenderScheduler::submit()`：拒绝非有限、或截断后为 0×0 的声明——View 的贴图尺寸来自
  这个 rect，所以「描述不了一个像素的声明不是 View」，失败留在声明边界，而不是图构建中途。

## 排查方法（值得复用）

- **SIGTRAP 没有断言文本**：调试构建的断言走异步日志，trap 时最后几行会丢。不要指望日志。
- 从 .ips 拿「出错指令地址」，对着**调试 dylib** 反查源码行：

  `atos -o build/<plat>/<arch>/debug/libya-render-graph.dylib -l <image base> <faulting addr>`

  其中 base 与 addr 都在 .ips 的 `usedImages` / `exception.codes` 里；这一步直接给出
  `RenderGraph.cpp:1027`，比猜快得多。
- **lldb 会掩盖这类 bug**：单步/慢速启动改变时序与堆布局，同样的序列在 lldb 下 0 复现。
  要做的是加**只在出错条件命中时**才写的 stderr 诊断（`fprintf` + `fflush`，并且不要每帧刷），
  从声明侧与消费侧同时打印那个值，就能分清「源头是垃圾」还是「传递中变坏」。
- 「重试一次就好了」不是结论：这次第二次重试就复现了。可疑的 flake 必须查 crash report，
  并把它写进 smoke 序列（game→editor 这一对属于必须覆盖的顺序）。

## 不变量

- 任何 `glm` 成员都要有初始化器；`Rect2D` 这类「稍后才有值」的几何类型由类型自己兜底。
- 一个 View 的 rect 必须是**整数像素语义**：`Extent2D::fromVec2` 会截断，所以
  `> 0` 不代表「有一像素」。声明边界（scheduler）负责拒绝，声明方负责给出合法值。
