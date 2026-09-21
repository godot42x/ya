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

## Phase F2（已落地）

编辑器在非 runtime 模式下声明的 authoring view 没有 composeOntoViewId，因此它就是
display root：present 会把整张世界图全屏拷到 swapchain，紧接着
recordDisplayExtensions 里的编辑器 chrome 把整个窗口盖满。那次全屏拷贝纯浪费。

修法是先把语义摆正，再让宿主做决定：

- 「窗口底板是什么」是宿主事实，不是渲染器能从 View 结构推断的。
  `PresentFrameInput::bCopyViewDisplayImage` 就是这条声明，默认 true（宿主什么都不说
  ＝窗口显示该 View，即今天独立 runtime 的路径）。
- `PresentationGraphService::recordDisplayCompose` 按它决定是否把 View 的 display
  image 铺满 surface。false 时不拷贝、也不去问 provider，surface 只剩本 pass 自己的
  clear，`recordDisplayExtensions` 里的 chrome 就是整个窗口。pass 本身照常跑，
  chrome 也照常在里面录制，顺序没变。
- 谁在铺 surface 就由谁回答：`IRuntimeModule::fillsPrimarySurface()`（默认 false），
  `App::presentsViewDisplayImage()` 遍历模块得到答案，
  `GameRuntimeTickOrchestrator::recordFrame` 把它填进 plan。是查询而不是让模块自己翻一个
  开关，所以不存在「这一帧的答案」和「这一帧实际录了什么」不一致的窗口。
- 判据不能用 AppState。用 `isRuntimeMode()` 是第一版写法，它是错的：编辑器工具栏的
  Play 走 `App::startRuntime()`，也就是「编辑器内 runtime」——窗口仍然是编辑器的，
  chrome 照样铺满，只是 viewport widget 里换成游戏那张 View。用 AppState 判会让 Play
  期间又退回一次全屏白拷。现在编辑器不管处于哪个状态都答 false。

顺带把误导性的命名改掉（上一版 F1 之后它们已经不成立）：display compose 用的
`BasicPostprocessing` 实例在 passThrough 配置下就是一次采样拷贝，所以
`_presentationPostProcessor` -> `_displayImageCopy`、`_presentationToneMap` ->
`_displayImageCopyBindings`、描述符池 label `Presentation_ToneMap_DSP` ->
`Presentation_DisplayCopy_DSP`。

验收（本轮实测）：

- 一次性探针（改完即撤）逐帧打印，并存打印当时的 AppState 与判据：
  - 独立 runtime：20/20 帧 `copy=1 source=1`（窗口就是 View）。
  - 编辑器 Stopped：全部 `runtime_mode=0 presents_view=0 copy=0 source=0`。
  - 编辑器内 Play（通过 automation control 口 `set_app_state runtime` 真实进入）：
    `runtime_mode=1 presents_view=0`，`copy=0` 持续成立 —— 这正是 `isRuntimeMode()`
    这一版会判错、退化成 `copy=1` 的那一帧情形。整场 2839 帧没有一帧 `copy=1`。
- 独立 runtime 门禁仍 PASS：viewport 与 presentation 都是 `c775245a…`，与 F1 之前基线
  逐字节相同，说明 runtime 路径没被扰动。
- 编辑器 presentation 截图（HUD 区域除外，见下）改动前后逐字节相同，改动后三次运行也
  逐字节相同：chrome 本来就是不透明铺满，所以被删掉的那次拷贝确实一个像素都没露出来。
- `xmake r ya-render-3d-test` 175/175；宽滤镜 696 跑 679 过（新增一个用例），失败集与既有
  基线一致。
- `AppLifecycleTest.TheSurfaceBackdropIsWhatTheLoadedModulesSayItIs` 锁住策略与它的分支：
  没有模块铺 surface → true；有模块铺 → false；模块改成不铺 → 又回到 true。

已知截图不可复现来源（本轮实测确认）：编辑器 presentation 截图每次运行都会在
Frame Inspector HUD 的实时计时文字上漂移（默认开，profile 构建）。本轮比对把
x 1140..1240 / y 735..776 排除在外；这段之外两次运行逐字节相同。

未归属的观察：本轮第一次用 `control start` 拉起编辑器实例时，进程在首帧前后以
SIGBUS / `KERN_PROTECTION_FAILURE` 死在 `App::presentsViewDisplayImage()` 里（崩溃栈里
出错地址落在 dyld shared cache 的 `libc++abi` 区域）。同一条命令随后重复多次、以及在
该实例里真实进入 Play 之后连续跑 2800+ 帧都不再复现；该函数本身只是一个只读遍历。
倾向于把它记成构建/映射层面的偶发（正在跑的进程其 dylib 被重新链接），但没有查实，
所以记在这里而不是抹掉：如果后续再看到同一处崩溃，这才是起点。

## Phase F3（已落地）

F1 只把 present 的 grading 关掉了，没有把它**移走**：surface 这一层仍然持有一个
postprocess 管线实例和一份 state。`passThrough()` 意味着「现在不做」，不是「这里不能做」
——F1 的根因（present 能再 grade 一次）在架构上仍然可达。

这一轮把后处理从 surface 这一层删掉，并给 surface 写入加一道强类型门禁：

- **窗口底板用类型表达，不用布尔。** `FSurfaceImage` = 图像 + `EImageEncoding`
  （`Linear` / `DisplayEncoded`）。格式说不出的那件事（值是什么含义）随图像走。
  `RenderDeviceState::getViewDisplayImage()` 由渲染侧回答；`display` 缺失回退到 raw color 时
  如实标成 `Linear`，而不是让 surface 猜。
- **门禁是配对检查，不是布尔。** `findSurfaceImageMismatch(image, surfaceFormat)`：只有当
  `DisplayEncoded` 撞上会做写入编码的 surface（sRGB）时才拒绝——那会让硬件再编码一次。
  另外三种配对都合法，其中 `Linear` + sRGB 就是「硬件来编码」的正路，不是异常。
  不匹配时拒绝并报错，pass 照常跑：surface 保留 clear，host 的 chrome 不受影响。
  选拒绝而不是断言，是因为「present 了错的图」除了颜色不对没有别的症状。
- **pipeline 从 presentation 移走。** `PresentationGraphService` 不再持有任何管线、state、
  descriptor pool，也不再 include `BasicPostprocessing` / `PostProcessingState`；它只有
  swapchain 导入、per-image executor、clear、host 内容、capture。谁来画底板由
  `ISurfaceBackdropWriter`（在 `SurfaceImage.h`）表达，实现是渲染侧的 `SurfaceWritePass`
  ——postprocess 家族里的一次写入，复用现成的 `BasicPostprocessing`（`passThrough`），
  没有新 shader、没有新管线类型。
- **注入点回到 InitDesc**：`backdropWriter` 默认 null。null 是真实答案（host 自己铺满窗口），
  此时 surface 只剩 clear。

F2 那一版的 `PresentFrameInput::bCopyViewDisplayImage` 在这里升级成 `ESurfaceBackdrop`
（`ViewDisplayImage` / `HostContent`）：同一个事实，但用类型表达，和「底板是什么」的
`FSurfaceImage` 是同一套词汇。

过程中撤掉的一版：先做了 `SurfaceResample`（独立 shader + 独立管线类）。它不必要——
表面写入是 postprocess 形状的活，复用 `BasicPostprocessing` 即可，多一个管线类型只是多一份
「以后可以把 grade 塞回来」的地方。

验收（本轮实测）：

- runtime 门禁仍 PASS：viewport = presentation = `c775245a…`，与 F1 之前基线逐字节相同。
  surface 写入从「新 shader」换回复用的 postprocess 管线，输出逐字节不变。
- 编辑器 presentation 截图（排除 HUD 计时带）改动前后逐字节相同，两次运行也相同；
  没有任何 refused 日志。
- `ya-render-3d-test` 177/177（新增 `SurfaceImageTest` 两例，锁住四种配对里只有
  「已编码的图 + 会编码的 surface」被拒绝，以及 `isSRGB` 是门禁的唯一依据）。
- 宽滤镜：698 跑 692 过，失败集与既有基线一致。

## 明确不做

- 不把 game UI 合成并进 present 以省一次全屏 pass。View 的 display image 是会被多方采样的
  单位（编辑器视口、PIE、未来多窗口各自的 UI scale、自动化截图），UI 烤进 View 输出是有意为之。
- 不改「UI 进 View RT、post 之后」这个落点。它与 Unity 的 Screen Space-Camera、UE 的
  post-Slate、Godot 的 CanvasLayer 同类，问题只在 present 又 grade 了一次。
- 不把 surface 写入再抽一层「surface pass 抽象」或新的管线类型。它是 postprocess 家族里的
  一次写入，`ISurfaceBackdropWriter` 已经是它需要的全部接口面。
- 不为「surface 可能是 sRGB」去改 RHI 或强迫 swapchain 格式。门禁现在会把这种情形**报出来**
  （`DisplayEncoded` + sRGB 被拒绝），要不要支持「线性图 + 硬件编码」这条正路是独立决定。
