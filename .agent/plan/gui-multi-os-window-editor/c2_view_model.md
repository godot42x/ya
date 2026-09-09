# Camera 渲染链：graphics → UI → compose → present

> 2026-09-09。接 [`c2_present_compose_model.md`](c2_present_compose_model.md)。方向索引：[`plan.md`](plan.md)「冻结：device / present / camera」。
> 一条管线按 Unity Camera 组织：**单位是 Camera，不是 OS 窗口，也不是全局 “viewport”**。N Camera 实现冻结到 C2 之后（`MW-902`）。

## 链

```text
Camera
  → graphics pass     世界（Forward/Deferred + post），写 Camera 的离屏 RT
  → UI pass           该相机上的 Game UI / Screen-space UI（post 之后，不进 bloom）
  → view compose      overlay 相机、editor gizmos，仍写这台相机的 RT
  → display compose   仅当这台相机要出现在某扇 OS 窗上：把若干 image 排到 swapchain
  → present           该窗 IRenderSurfaceContext 选 swapchain image 并 present
```

Camera 的输出可以 **停在离屏 RT**（给 ViewportWidget 取样、给另一台相机当输入、给 Material 预览）。只有 “这扇窗这帧要上屏” 才走 display compose + present。

禁止把 present / swapchain 伸进 graphics pass。禁止用一扇窗的尺寸去驱动所有 Camera。

## 两段 compose（名字不要混）

| 名字 | 写到哪 | 谁拥有 | 例子 |
| --- | --- | --- | --- |
| **View compose** | 这台 Camera 的离屏 RT | Camera / WorldView | 游戏 UI、gizmo、overlay camera |
| **Display compose** | `swapchain[imageIndex]` | PresentSurface | 全屏游戏把唯一相机 RT blit 上屏；编辑器把 chrome snapshot（内含 ViewportWidget 取样的相机图）上屏 |

`PresentationGraphService` 只做 **主 world 窗的 display compose**。辅助 GUI 窗：chrome snapshot → 自己的 swapchain，不经它。

## 对象（从旧 “viewport” 拆出）

```text
Camera / WorldView     一台相机 + 离屏 color/depth/post RT + 上面那条链
PreviewTarget          不是场景 Camera：材质球、UI 画布；仍可走 graphics→(optional UI)→停在 RT
PresentSurface         OS 窗 swapchain；只参与 display compose + present
WindowChrome           该窗 WidgetTree；编辑器 dock/inspector，不是 Camera UI pass
ViewportWidget         chrome 里取样某 Camera/PreviewTarget 输出图的控件（布局矩形 ≠ RT 尺寸）
```

`RenderRuntime`：device 上串行执行若干 Camera 链。不按窗口复制，不为 Material/UI 窗复制 GBuffer。

今天的单槽：`ViewportState` + 一套 pipeline RT = `WorldView[0]` / 唯一 Camera。

## 映射到现有 `renderFrame`（单 Camera）

`RenderRuntime::FrameInput` 已分组（R-1）；submit 仍是一次。R-3 把后两段 compose 收成具名录制：

```text
CameraFrameInput.view/projection/viewProjection/extent  graphics pass（owner 在 graph build 前算好）
recordCameraViewCompose(cmdBuf, cameraDisplayRT, camera, viewCompose)
    CameraFrameInput.uiFrameSnapshot                    UI pass（该 Camera 的 RT）
    ViewComposeInput.recordCompose                      view compose（editor overlay）
PresentationGraphService::recordDisplayCompose          display compose（swapchain[imageIndex]）
Host FPresentFrame acquirePresentFrame / submitPresentFrame
PresentFrameInput.surface + imageIndex                  已 acquire 的 present 目的地（RenderRuntime 不 begin/end）
```

多 Camera 时：对每个要画的 Camera 重复 graphics → UI → view compose；然后 **每个** PresentSurface 各做一次 display compose → present。

## 产品

| 产品 | Camera 链 | Display |
| --- | --- | --- |
| 全屏游戏 | 1 Camera，extent = 窗口 framebuffer（host 写） | 该 RT → 主窗 swapchain |
| 分屏 / 监控 | N Camera，各自 extent；aspect 只改 **绑定** 的那台 | 一扇窗 display compose 多张 RT，或 HUD 取样 |
| Level 编辑器 | 1+ 场景 Camera；ViewportWidget 取样输出 | chrome snapshot → 该编辑器窗 swapchain |
| Material 窗 | PreviewTarget（预览 mesh Camera），不是 level GBuffer | 自己的 chrome + present |
| UI designer 窗 | PreviewTarget = 被编辑树的 offscreen snapshot | 自己的 chrome + present |

`CameraComponent.bPrimary`：默认哪台相机驱动 `WorldView[0]`。不是 “全引擎只有一个 viewport”。
`syncRuntimeCameraAspect` 扫全场景写同一 aspect —— 多 Camera 前必须改成只写绑定相机。

## 禁止

- graphics / UI pass 读写 swapchain
- 用 ViewportWidget 布局去改未绑定相机的 aspect
- 为每扇 editor 窗 `IRender::create` 或复制 `RenderRuntime`
- 把 `getViewportDisplayImageShared()` 当成 “这扇 OS 窗的画面”
- 把 WindowChrome（dock）塞进 Camera UI pass

## 阶段

0. 本文件 + 现有单链标成 Camera 链。
1. `FRenderViewDesc` / 显式 camera↔view（仍执行 1 条链）。
2. 同一 `RenderRuntime` 串行 N 条 Camera 链。
3. 分窗：ViewportWidget 绑 ViewId；Material/UI 走 PreviewTarget。

本轮不实现 N Camera、不拆 acquire 出 `RenderRuntime`、不改 MW-202 / ES-1 / Feature Gallery。

**冻结（2026-09-09）**：Camera 链对象模型到此为止。在 RHI 多窗（C2 MW-202/203 + extra present）完成前，不启动 `FRenderViewDesc`、不铺 N 条 Camera 链。那会是另一份计划，不是把本目录撑成 Camera 重构。
