# R4 — 规模与定案：测量记录

日期：2026-09-30。构建：**profile**。机器：Apple M5 / macOS 26.5（arm64）。
本文件是 R4 的唯一交付物（数据 + 决策）；R4 不改渲染代码。

## 1. 要回答的四个问题（plan §4 R4）

1. 精灵需要合批 / 实例化吗？
2. 每 View 16 张纹理表的上限是约束吗？
3. 纯 2D View 应该绕开 Deferred 吗？
4. tile 候选需要按区块缓存吗？

## 2. 方法

**夹具**：`Example/2DRpgPrototype/Content/Scenes/TownLarge.scene.json` — 64×64、3 层
（Ground 4096 / Decor 776 / Overlay 241）、20 个 NPC + 玩家，由
`Example/2DRpgPrototype/Tools/make_scale_scene.py` 确定性生成（同脚本可 `--size/--npcs`
造中间尺寸）。两个更小的夹具用于斜率：仓库里的 `Town.scene.json`（32×20）与临时生成的
32×32（命令见 §5，测完已删）。

| 夹具 | tile 候选 | 精灵实体 | 候选总数 | 不同纹理 |
| --- | --- | --- | --- | --- |
| Town | 696 | 4 | 700 | 2 |
| TownMedium（临时） | 1318 | 11 | 1329 | 2 |
| TownLarge | 5113 | 21 | 5134 | 2 |

候选数由**场景数据**算出（遍历图层非空格与带 Sprite2D 的实体），不是生成器的意图：
对应实现里「一个候选一次 draw」的规则（`Sprite2DStage.cpp:257`，
`drawSprites` 逐 `WorldSpriteCandidate` 一次 draw，只合并 pipeline 重绑），且 extractor
**全量展开不裁剪**（`RenderFrameExtractor.cpp:222` 起，`bHasVisibleRange` 未设）。
所以**候选数 = sprite pass 的 draw 数**：

- 逐候选一次 draw：`Engine/Source/Framework/Render/Render3D/Common/Sprite2DStage.cpp:257`
- 全量展开（无 View 裁剪、无区块缓存）：`Engine/Source/Applications/GameRuntime/Render/RenderFrameExtractor.cpp:222`

**帧时间**：profile 构建 + runtime CPU trace（speedscope），取 `iterate`（真正的帧根，
600 帧）在跳过前 60 帧后的均值/中位数/p95。分析脚本：本目录 `measure_trace.py`。

```bash
python3 Script/ya.py cfg --mode profile
python3 Script/ya.py run --project Example/2DRpgPrototype/2DRpgPrototype.yaproject -- \
  --scene=Content/Scenes/TownLarge.scene.json --exit-after-frame=600 \
  --log-level=warn --log-detail-level=error \
  --cpu-profile --cpu-profile-output=/tmp/r4_townlarge.json
python3 .agent/plan/rpg-prototype/measure_trace.py /tmp/r4_townlarge.json --skip 60 --top 20 --tree
```

**注意（读数的前提）**：runtime 默认开启帧限速（`FPSControl`，目标 120fps = 8.33ms，
`AppLifecycle.cpp:249`）。两个小夹具的帧时间正好顶在 8.33ms（受限速夹住），所以它们的
**绝对帧时间不可用**，只有 scope 内的时间可用；TownLarge 超出预算（10.56ms）所以它的
帧时间是真实值。`Tick/FpsControl` 的 self 时间就是限速 sleep，任何帧时间口径都不含它。

## 3. 数据

### 3.1 帧与主要 scope（ms/帧，profile）

| 夹具 | 候选 | iterate 均值 | p95 | `recordFamily` self | `Render/Frame` self | extract 场景快照 | Lua |
| --- | --- | --- | --- | --- | --- | --- | --- |
| Town | 700 | 8.312（限速夹住） | 12.94 | 1.769 | 4.259 | 0.018 | 0.028 |
| TownMedium | 1329 | 8.318（限速夹住） | 9.16 | 3.050 | 5.034 | 0.029 | — |
| TownLarge | 5134 | **10.561** | 11.95 | **7.409** | 2.858 | 0.104 | 0.015 |
| TownLarge @512×384 | 5134 | 9.782 | 10.95 | 7.463 | 2.023 | 0.107 | — |

`recordFamily` = `DeferredRenderPipeline::recordFamily`，即 Deferred 家族（G-buffer +
sprite pass + 光照 + 后处理）的**命令录制**；sprite pass 的 CPU 提交就在其中。
`Render/Frame` self = `RuntimeRenderContext::tick` 里没有单独 scope 的剩余部分
（帧栅栏等待 `beginRecordedFrame`、acquire、submit、offscreen pump 等，见 §6 未决）。

### 3.2 逐候选成本（三点拟合）

对 `recordFamily` self 与候选数作最小二乘：

```
recordFamily ≈ 1.14ms + 1.23µs × 候选数
   预测 700 → 2.00（实测 1.77）    预测 1329 → 2.78（实测 3.05）    预测 5134 → 7.45（实测 7.41）
```

点对点边际在 1.15 ~ 2.04 µs/候选之间（三点不完全共线，±10%），所以**报 1.2µs 为最佳拟合**，
并说明区间。换句话说：**5.1k 候选的 2D 画面，光 CPU 录制约 6.3ms，占 10.6ms 帧的 ~60%。**

### 3.3 窗口尺寸对照（区分「逐 draw 成本」与「填充成本」）

同一 TownLarge，窗口从 1024×768 缩到 512×384（像素数 1/4）：

- 帧 10.561 → 9.782ms（**−7%**）
- `recordFamily` 7.409 → 7.463ms（**不变**，候选数相同）
- `Render/Frame` self 2.858 → 2.023ms（−0.84ms，这是与填充/全屏 pass 相关的那部分）

即：缩小分辨率省下的 ~0.8ms 全在 GPU/填充侧；**帧的主导项（~7.4ms）与分辨率无关，是逐候选的 CPU 录制**。

## 4. 决策

### D-R4-1 精灵合批 / 实例化：**需要，且是当前最高价值的渲染改动**

数据：逐候选 ~1.2µs；5.1k 候选 → ~6.3ms（帧的 60%），且随候选数线性增长（三点拟合）。
纹理数只有 2（§3.2 表），说明这批 draw 几乎是「同一张图集、同一个 pipeline、参数不同」的
纯重复 —— 正是 instanced / indirect 的目标形态。优先级判断依据：**它是唯一随 2D 内容量
线性增长的成本项**，而内容量是本计划的验收方向（更大更密的地图）。

### D-R4-2 16 张纹理表：**当前不是约束，但查找方式要跟着合批一起改**

本场景只用 2 张纹理（tiny_town 图集 + hero 图），远低于 16 上限；被上限丢弃的候选
（`Sprite2DStage::drawSprites` 里 `slotFor` 返回 `kNoTextureSlot` 就 skip）在本场景为 0。
但注意：`buildTextureTable` 每帧重建，且 `slotFor` 对每个候选做**线性扫描**（当前最多 16 次
比较 × 5134 候选 ≈ 8.2 万次/帧）。合批实现时应顺手把它换成「纹理句柄 → 槽位」的直接映射，
并让上限超限成为可见诊断而不是静默 skip。

### D-R4-3 纯 2D View 绕开 Deferred：**不是当前的第一杠杆，但值得在图层面做**

数据：分辨率降到 1/4 只省 0.8ms，说明全屏的 Deferred 链（G-buffer / SSAO / Bloom /
tonemap）在这个 2D 场景里并不是瓶颈；瓶颈是逐候选提交。所以「为性能而绕过 Deferred」
在 5.1k 候选这个量级上收益有限（~1ms 级）。

但要分开两件事：绕开 Deferred 的**收益**小，不代表**图层面**不该拆。2D 场景跑 SSAO/Bloom/
阴影这套是为 3D 准备的，属于「为不需要的东西付分辨率成本」。建议：先做 D-R4-1；图层面
的拆分（一个只做 sprite + compose 的 2D 家族）按**正确性/清晰度**推进，不要拿性能当理由。

### D-R4-4 tile 候选按区块缓存：**现在不需要，4× 面积时再做**

数据：整帧提取（`sceneSnapshot`，含 5134 个候选的构造与排序输入）只要 0.10-0.15ms，
是 draw 提交的 ~1/50。64×64 全量展开的 CPU 成本可以忽略；按 4× 面积外推（256×256，
~8 万格）约 0.6ms，届时才值得做区块缓存或 View 裁剪。**先做 D-R4-1**：缓存提取结果
不会减少 draw 数。

## 5. 复现（含临时的中间尺寸夹具）

```bash
# 中间尺寸夹具（测完删掉，本文件记录命令；不随仓库提交）
python3 Example/2DRpgPrototype/Tools/make_scale_scene.py --size 32 --npcs 10 \
  --out Example/2DRpgPrototype/Content/Scenes/TownMedium.scene.json
# 然后按 §2 的命令跑，把 --scene 换成 TownMedium，再删掉该文件

# 窗口尺寸对照
python3 Script/ya.py run --project Example/2DRpgPrototype/2DRpgPrototype.yaproject -- \
  --scene=Content/Scenes/TownLarge.scene.json --width=512 --height=384 \
  --exit-after-frame=600 --log-level=warn --cpu-profile --cpu-profile-output=/tmp/r4_smallwin.json
```

限速开关：`Engine/Saved/Config/Editor.json` 的 `runtime.framePacing.enabled`
（本地配置，本次**没有改动**；上面所有数字都在默认限速下取得，口径见 §2 的注意）。

## 6. 未决与交接（给 `scene-2d-world-and-game-ui`）

1. ~~`Render/Frame` 的 self 时间没有归因~~ → **已归因（2026-09-30，见 §7）**：主体是
   `VulkanRender::waitFrameFence`（CPU 等上一帧 GPU，`kFramesInFlight = 1`），不比 D-R4-1 大。
2. 本次 draw 数是**按实现规则推导 + 场景数据计数**（逐候选一次 draw、全量展开），
   不是运行时计数器 —— 环境里没有 RenderDoc（`Script/renderdoc/rdc_pass_summary.py`
   需要 `renderdoccmd`）。若要长期量化，建议加一个只读诊断计数（候选数/纹理表命中率）
   走 automation，而不是每次靠推导。
3. D-R4-1 的目标形态（instanced quad / indirect + 纹理索引）由接收方定；
   本计划只提供「成本随候选数线性增长、纹理高度集中」这两条依据。

## 7. `Render/Frame` 归因（2026-09-30 补测）

`RuntimeRenderContext::tick` 里原来没有 scope 的步骤都补了 `YA_PROFILE_SCOPE`（`Render/DeclareViews`、
`Render/ExtractScenes`、`Render/BuildGameFrame`、`Render/BeginRecordedFrame`、`Render/AcquirePresent`、
`Render/ExtraSurfaces`、`Render/SubmitPresent`），`VulkanRender::beginRecordedFrame` 内拆出
`VulkanRender::waitFrameFence` 与 `DeferredDeletionQueue::flush`。补测命令同 §2（profile，600 帧跳 60），
构建已含双面 quad 修复（精灵 / 光照全屏 pass / billboard 只光栅化一次）。

| 夹具 | iterate 均值 | `recordFamily` self | `waitFrameFence` | `SubmitPresent` self | `Render/Frame` self |
| --- | --- | --- | --- | --- | --- |
| Town | 5.900 | 1.920 | 0.524 | 0.645 | 0.006 |
| TownLarge | 10.513 | 7.359 | 2.184 | 0.521 | 0.006 |

- `Render/Frame` self 降到 0.006ms，原来的 2.9–5ms 已全部落到具名步骤。
- 主体是 **帧栅栏等待**：`kFramesInFlight = 1`，CPU 录下一帧前要等上一帧 GPU 做完，GPU 时间串进 CPU 帧。
  本机 MoltenVK 拿不到 GPU 时间戳（`tickGpuMs` 恒 0），只能由等待反推：TownLarge 的 GPU 帧约
  `waitFrameFence + SubmitPresent ≈ 2.7ms`。调高 `kFramesInFlight` 是 CPU/GPU 重叠决策，
  归 `render-view-family/temporal_semantics.md` M4（所有 per-frame 环一起轮转验证），这里不动。
- 限速口径与 §2 的注意不一致：`Engine/Saved/Config/Editor.json` 配的是 `fpsLimit: 60`，而 Town 帧均值
  5.9ms、`Tick/FpsControl` self 2.5ms。`FPSControl` 的实际行为未查；各 scope 的读数不受影响，
  但小夹具的绝对帧时间仍不要拿来比较。
- **更正 §3.1 的"extract 场景快照"一列**：R4 用的 scope `RenderFrameExtractor::sceneSnapshot`
  实际包的是逐 View 的 `prepareViews`（已改名 `Render/PrepareViews`，TownLarge 0.141ms）；
  真正的 ECS 快照抽取 `Render/ExtractScenes` 原来没有 scope，TownLarge 0.051ms。
- 资源准备（`RenderDeviceState::prepareDerivedState`，含三个处理器）TownLarge 0.011ms/帧：
  这个场景带资源的实体很少，轮询的 CPU 成本可以忽略。
- 结论：D-R4-1（逐候选提交 7.4ms）仍是最大项，优先级不变；第二项是 CPU/GPU 串行（约 2.2ms），归 M4。
