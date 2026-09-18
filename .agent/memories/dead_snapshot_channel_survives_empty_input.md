# 没有生产者的 snapshot 通道能带着一整条 pass 活很久：空输入是合法输入

> 2026-09-19，track "逻辑→渲染链减法" 时定位。删除的是 2026-09-19 之前一直存在的主机
> screen-overlay 通道；不是本次重构引入的回归，是长期存在的死通道。

## 现象 / 危害

`FramePacket::OverlayInput` 有四路可选 vector（screenSprites / worldSprites / screenTexts /
worldLines），Forward 与 Deferred 各有一条 `kTopologyPassOverlay` 会在这些输入上 append
一个 raster pass。真实情况是：

- 四路输入的**唯一生产者**是 GameRuntime 里一段不可达的 demo（`AppMode::Drawing` + 点击记录屏幕坐标）。
- `buildViewportOverlaySnapshot` 在四路全空时返回 `nullptr`。
- pass 在 `nullptr` 下**照常 append 并录制**：每帧多一个空的 raster pass（begin/end rendering、
  一次 attachment load/store 声明），在所有被测路径上都是如此。

于是删除它不改变任何像素（两张 smoke 截图逐字节相同），但每帧都在跑一个没人用的 pass。

## 根因

**"四路可选 vector" 让"没有生产者"和"这一帧没有 overlay"在代码里长得一样。**

- 输入可选 + 空是合法值 ⇒ 没有断言、没有日志、没有测试能发现它。
- 生产者删掉之后消费者仍然"工作"，因为它本来就一直在处理空输入。
- 这条通道横跨 host（组装）→ FramePacket（携带）→ ViewFamilyRecordContext（传参）→
  RenderPipelineFrameContext（再传）→ BuildInputs/PassParams（再传）→ pass（消费），
  所以"读一遍链路"时每一段都像是"别人会填"，没有一段能独立判断它是不是死的。

另一条同源小坑：`resolveViewportExtent` 的三段兜底里，`device->getViewportExtent()` 在
init 之后恒非零，所以后两段（viewportRect、窗口尺寸）永远不可达——函数看起来"有回落策略"，
实际是"恒返回上一 tick 发布的尺寸"。兜底链的每一段都要回答"这一段的输入什么时候会缺失"。

## 处置

- 删除整条通道（类型、字段、上下文变量、Forward/Deferred 的 pass、perf key、相关测试），而不是
  为它补一个消费者。
- 保留 `RenderOverlayText2D` / `RenderOverlayLine3D` 这两个**值**：编辑器 HUD 与选中相机视锥
  仍然画它们，但由录制 overlay 的一方在自己的 viewport compose 里直接读，不再由 renderer 携带
  snapshot。

## 通用判据（已进 `skills/render-arch` 规则 15）

1. 新增"可选的输入通道"时，先写下**生产者是谁**；找不到生产者的字段 / 通道 / pass，出现即删。
2. 兜底链的每一段都要能回答"这段输入何时缺失"；恒非空的分支就是死代码。
3. 观测手段：把该通道的输入打印出来跑一次 smoke。如果产品路径上它恒为空，那它要么缺生产者的实现，
   要么就是死代码——两种结论都不能靠读代码定，要靠"生产者是谁"回答。

## 边界

- 可复用的系统设计、长期工作流写入 `../skills/*/SKILL.md`
- 一次性故障、回归根因、项目历史坑写入 `./*.md`
