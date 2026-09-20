# Display Compose Encoding

## 目标

让 display compose 只搬运、不改颜色：把「View 的 display image 已经是 display-ready」
变成一条不变量，而不是每个消费者各自判断。

## 为什么这是问题

一帧的最终图像经过两次「让它可显示」的处理：

```
today:  world graph -> [finalize?] -> game UI -> present(grade)   finalize 可被跳过
after:  world graph ->   finalize  -> game UI -> present(copy)    一条路径，一个不变量
```

证据（修复前，同一帧的两个截图目标逐像素配对：presentation 是 viewport 的单值函数，
同一输入亮度 min == max）：

| viewport 输入 | presentation 输出 |
| --- | --- |
| 32 | 82 |
| 64 | 127 |
| 96 | 156 |
| 128 | 176 |

按公式校验：64/255=0.251 -> x0.6 -> ACES -> gamma -> 0.4998 -> 127.5。也就是说窗口上
又做了一次 ACES + 二次 gamma，而编辑器视口（直接采样 display image）没有。Game UI 是
被合成进那张图的，所以 UI 颜色也跟着被冲淡。

## 根因

两个独立的原因叠在一起：

1. **两份 state。** 编辑器改的是管线的 post state（写进 Runtime.json）；present 用的是
   PresentationGraphService 里一份从未被配置过、界面上也看不见的默认值
   （ACES / exposure 0.6 / gamma 开）。
2. **display image 的性质随模式变。** PostProcessingStage 在关闭时让 finalize 整个
   早退，于是 display 退回成 R16G16B16A16_SFLOAT 的线性 HDR 原图。present 的 grade
   本来是在补这个洞，结果把正常路径弄坏了。

## Phase F1（已落地）

让 finalize 无条件运行，present 退化成纯透传：

- PostProcessingState 增加两个语义构造：passThrough()（输入已编码，任何 flag 都是
  二次 grade）与 withoutGrading()（关掉 grading，保留 display 编码）。
- PostProcessingStage：bEnabled -> bGradingEnabled。grading 关掉不再跳过 finalize，
  只把 inversion/grayscale/kernel/tonemap/grain/bloom 归零；gamma 作为 display 编码保留。
- 两条管线：display image 恒为 finalize 输出，不再回退到 raw color。
- RenderDeviceState::getViewDisplayImageFormat()：恒为 postprocess 格式，模式分支塌掉。
- PresentationGraphService：display compose 用 passThrough 配置，删掉那份平行 state。

验收：

- 门禁 Script/automation/render/run_display_compose_parity.py：同一帧的
  --screenshot-target=viewport 与 =presentation 必须逐字节相同。
- viewport 截图必须与 F1 之前的基线逐字节相同（证明 View 路径没被扰动）。

## Phase F2（未做）

编辑器在非 runtime 模式下声明的 authoring view 没有 composeOntoViewId，因此它就是
display root：present 会把整张世界图全屏 blit 到 swapchain，紧接着
recordDisplayExtensions 里的编辑器 chrome 把整个窗口盖满。世界图渲了两次，第一次纯浪费。

修法：让宿主显式声明「这一帧我自己画满屏 chrome」，不要在 present 里按模式猜。
验收：编辑器 pass 计数下降 + 冒烟图不变。

## 明确不做

- 不把 game UI 合成并进 present 以省一次全屏 pass。View 的 display image 是会被多方采样的
  单位（编辑器视口、PIE、未来多窗口各自的 UI scale、自动化截图），UI 烤进 View 输出是有意为之。
- 不改「UI 进 View RT、post 之后」这个落点。它与 Unity 的 Screen Space-Camera、UE 的
  post-Slate、Godot 的 CanvasLayer 同类，问题只在 present 又 grade 了一次。

