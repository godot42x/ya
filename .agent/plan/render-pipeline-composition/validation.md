# 验证、基线与证据

> 本文件给出执行参照，不表示命令已经跑过。2026-10-12 本轮为计划落盘，无新 C++ / GPU 验证。

## 1. 证据目录与记录格式

执行时创建 `evidence/<checkpoint>/<platform>-<mode>/`，记录小型报告和可复现配置；大型日志 / trace / PNG 的保存策略按仓库现有规则，报告必须给出可定位路径或 hash，不能只有“看起来一样”。不预先制造空 baseline 文件。

每份报告至少包含：

- commit + 工作区相关 diff（未提交灯光变化必须记录），平台/GPU/驱动、mode/profile、分辨率。
- 实际命令、退出码、测试数、相关失败及是否 baseline 已存在。
- 场景 / 相机 / 设置 / 随机或时钟条件、warmup、截图对应 tick 与 pipeline 身份。
- composition 调用数、实际 pass/read-write 关系、GPU allocation / replacement / retirement 可观测值及来源。
- screenshot 差异结果、CPU/GPU 指标 before/after、不可比较 / 不支持项。
- 当前 checkpoint 保留桥接、未测项、偏离和阻塞。

历史 P2 的 188 passed / 240 frames / flush 帧号只说明当时结果；不得复制成新基线。

## 2. V0：C1 写代码前的 baseline

1. `git status --short` 和相关 diff；确认 shared workspace 灯光线改动的归属与整合版本。
2. target/project 当前仍存在；不依赖计划里的旧 target 清单。
3. Render3D 全量测试；Runtime / Editor closure build；HelloMaterial Deferred、Forward 各自冒烟。
4. 固定配置获取 AO 开/关、Bloom 开/关、grading 开/关的原始画面 / topo；每条管线对照自己，不要求两条管线互相一致。
5. import trace 下观察首次创建、异步资源完成、稳定态、resize、切 pipeline；确认现有 failure 后再开工。
6. 收集本次机器的 CPU/GPU baseline，若模式切换使用独立 worktree，不改共享 xmake 配置。

V0 是 C1 的前置证据，不是独立“进度”提交。相关 baseline 失败且不能归因时暂停；无关失败记录后不擅自修。

## 3. 当前已存在的命令入口

先读 `skills/ya-build`；需要 profile 时串行切到 `skills/profiling`。存在并发构建时，下面的 build/run 用 `Script/agent/ya_lock.sh run -- <command>` 包住；不能两份构建同时使用同一 xmake 目录。

```bash
git status --short
xmake l targets
xmake b ya-render-3d-test
xmake r ya-render-3d-test
xmake b ya-game-runtime
xmake b ya-game-editor
python3 Script/ya.py run --project Example/HelloMaterial/HelloMaterial.yaproject -- --exit-after-frame=240 --max-lifetime-seconds=120 --render-graph-trace=true --log-level=trace
```

普通单测开发可用 filter，checkpoint 完成仍跑 Render3D 全量：

```bash
xmake r ya-render-3d-test --gtest_filter='PipelineCompositionTest.*:PipelineFeatureContractTest.*:PipelineRecipeTest.*:ViewTargetStoreTest.*:RenderSubmissionTest.*:RenderGraphCoreTest.*'
```

`Pipeline*Test` 是 C1 起拟新增 suite，不是已存在测试。若实际命名不同，实施同批更新命令；不能允许 filter 匹配 0 个用例却记为通过。

跨 Runtime 接口的 C1/C4 需要 integration 回归（先确认实际 suites）：

```bash
xmake b ya-testing
xmake r ya-testing --gtest_filter='HostSceneRenderSubmitTest.*'
```

C4/C5 另跑 test group，遇 baseline 无关红项要分别记录，不能宣称全绿：

```bash
xmake b -g test
xmake r -g test
git diff --check
```

只改变生成链时执行 `xmake ya-shader`；本线不手改 generated，也不为组合层测试强行重新生成全部 shader。

## 4. 运行时设置 / 截图的真实入口

已有 `--automation-config=<path>` 从 `GameRuntime/Lifecycle/AppAutomation.cpp` 解析以下 JSON 字段，可在 evidence 配置中明确写出：

```json
{
  "smoke": {
    "deferred": { "ssao": { "enabled": true } },
    "postprocess": { "enabled": true, "bloom": { "enabled": true } },
    "renderPipeline": { "target": "forward", "frame": 120 },
    "renderResolution": { "width": 800, "height": 600, "frame": 60 }
  }
}
```

此配置用于切换 / resize 冒烟，不是稳定画面对照的全部配置。对照分别固定一种 pipeline、固定 resolution / camera / 内容 / 时钟，排除 grain 或动画噪声。`smoke.postprocess.enabled` 当前含义是 grading 开关，不能误认为取消 Finalize。

长驻截图 / 场景驱动走现有控制入口，不新增服务器：

```bash
python3 Script/ya.py control start --project Example/HelloMaterial/HelloMaterial.yaproject
python3 Script/ya.py control call --project Example/HelloMaterial/HelloMaterial.yaproject set_render_pipeline '{"target":"forward"}'
python3 Script/ya.py control call --project Example/HelloMaterial/HelloMaterial.yaproject capture_screenshot '{"path":"<actual-evidence-path>/forward.png","target":"viewport"}'
python3 Script/ya.py control stop --project Example/HelloMaterial/HelloMaterial.yaproject
```

路径占位必须换成实际 evidence 路径；control start 可复用已有实例，必须确认实例/模式/状态，不能停止非本轮所有的实例。已有 `invoke` / `eval_js` / `list_commands` 可发现命令；不得假定某个未注册 `set_render_settings`、选相机或多 View automation 已存在。

SSAO / grading / Bloom 静态开关用 automation 配置或真实公开设置入口。运行中设置边界首先由 typed invocation 单测验证；若缺 runtime automation，就明确标记缺口，不能用单测替代 runtime 图证据。

## 5. 必须实现的行为测试

| ID | 场景 / 断言 | 阶段 |
| --- | --- | --- |
| M01 | 两个 heterogeneous feature 经 concept/add 保存 typed inputs/outputs；公开 API 无 feature 枚举 switch / blackboard | C1 |
| M02 | resolve 只一次；prepare 后请求新设置，本次 requirements/pass 使用旧 frozen 值，下一次使用新值 | C1 |
| M03 | SSAO off 无 AO requirement、UBO/DS/workload/新 publication；Lighting no-AO 使用中性 binding 语义 | C1 |
| M04 | 同一 resource declaration 生成 target request 并 lowering 为对应 import；改 format/usage 后二者一致，request declaration 重排不换 allocation | C1 |
| M05 | 缺 required input、不同 composition 端口、重复 role 冲突、错 tick/pipeline prepared token 被拒绝；optional absence 合法 | C1 |
| M06 | 插入 feature / 增减 View 不改变既有 import owner key；同 label 不同 scope 独立 | C1 |
| M07 | Invocation storage move / vector 增長后连接和 prepared 引用仍有效；callback 不依赖 builder 局部变量 | C1 |
| M08 | 两 View 使用不同 SSAO payload/upload/DS；View B 准备不覆盖 A；共享 frame/skinning 合法复用 | C1 |
| M09 | 多 pass feature 每次需更新的 descriptor 独立；blur 次数归一化边界和 scratch 来源明确 | C2 |
| M10 | 安全点开始本 tick publication，prepare 错误后查询为空；binding/build/compile/execute 不发布部分新 family；不得给失败帧标上 successful composition | C1 起 / C4 完整 |
| M11 | 同 backing ColorStream 续写→采样读写与旧图一致；Deferred Overlay 落在 Bloom 后且当前 Bloom.Composite backing 被 Overlay 续写，诊断输出为 Overlay 后内容（与旧行为一致）；Forward 颜色位置不变 | C2, C3, C4 |
| M12 | grading off 仍有必需 Finalize/DisplayColor，encoding 与现有语义一致；色彩兼容路径不吞失败 | C2 |
| M13 | declared/allocated-but-skipped 输出不发布旧 image；通用 output collector 替代两套 feature 名称解析 | C1 起 / C2 完整 |
| M14 | Bloom 只作为 multi-pass 契约样本：新增私有 scratch / 改局部描述不改 pipeline、registry、executor | C2 |
| M15 | Deferred recipe 的完整 map；GBuffer/Light/skybox/sprite/overlay/entity-id/post 输入与旧行为一致 | C3 |
| M16 | Forward recipe 完整 map；shading/debug/direction/skybox/entity-id/颜色顺序与旧行为一致 | C4 |
| M17 | 当前 local-light generated 类型、字段映射和预算回归保留；Shared View payload 不重复上传 | C3/C4 |
| M18 | Shadow token / strategy owner 与 view invocation 分开；两 recipe 无 prepare/append 旁路；同 Scene 多 View/不同 Scene 隔离 | C4 |
| M19 | resize/switch/reload/unregister、absence/no extent、empty/UI-only/no surface；旧 generation 保活到 fence，未注册不可查询 | C3/C4 |
| M20 | 组合诊断源于 actual invocation，状态/原因/连接/owner/pass/有效 outputs 可对账；保存后的 strings 不悬空 | C5 |
| M21 | 测试专用 producer→multi-pass transform→consumer 全链路扩展，无 core / all-feature bag 修改 | C5 |
| M22 | 两 pipeline runtime 多 View 真正实跑，资源/DS/输出不串；不是仅单测 scope 覆盖 | C4/C5 |
| M23 | diag off 性能与 GPU allocation/replacement/retirement 基线无回退，旧 adapter/context/bag 全退出 | C5 |

不同 semantic 端口的不兼容应由编译期 concept/type 检查证明（可用 requires/static_assert），不是运行时随意 cast 后测到失败。具体 shader draw 正确性仍靠真实运行时证据。

## 6. 截图门禁

- Deferred：SSAO on/off、Bloom on/off、grading off、sprite+opaque 遮挡、overlay/gizmo、entity-id/picking（图像加行为断言）。
- Forward：Bloom on/off、grading off、shading models/skybox/debug/direction、entity-id。
- 同管线同条件 baseline 至少重复 2 次，先确定自身确定性 / 噪声范围，再比较 after。
- deterministic case 目标为像素一致；若不一致报告差异像素数、最大误差、区域和原因。容差必须来自 baseline 噪声并先记录，不能看 after 再放宽。
- 不修正 Forward/Deferred 既有差异；空 draw pass 的“画面一样”不足以证明资源顺序可以删。
- 多 View 优先使用可控 integration fixture 实际走 family record（可新增测试专用 View producer），或编辑器 authoring+preview；如果 backend fixture/选择入口缺失，列 blocked，不能最终宣称全绿。

## 7. 生命周期与性能门禁

### 生命周期

- View store exact request reuse；resize / role 集合变动按原整组 replacement，一次换 generation。
- publication token 在安全点开启；准备错误、合法空 plan、command begin 错误不能让当前 flight 的旧 tick 图被当成本次结果。
- submission 整体 retain allocation；unregister 撤掉 publication，不破坏在途旧引用。
- sampled image/view/sampler、descriptor 数据、PSO/buffer 活到既有提交完成 / fence safe 退役边界。
- 稳态 GPU allocation/replacement 应归零（按已完成资源加载场景）；真实扩容、resize、退出分开统计。
- 同名不同 scope、View 增减、feature 开关不产生 backing 未变却误 replacement。使用现有 registry trace，不重新设计判定。
- 不把 CPU invocation record allocation、wrapper-retain 变化、DDQ flush 直接等同 GPU allocation；缺观测项就标明，不能编造计数。

### 性能

复用现有工具，在独立 profile worktree、同 GPU/分辨率/设置/内容中收集至少 3 次 before/after。具体配置先核对现有 benchmark 能否覆盖 recipe 工作量；不可把只有 Deferred 场景的结果冒称双管线。

```bash
python3 Script/perf/bench_run.py --project Example/PerfLab/PerfLab.yaproject --bench-dir Example/PerfLab/Benchmark --filter Lights_50 --out <actual-evidence-path>/before --repeat 3
python3 Script/perf/bench_compare.py --before <actual-evidence-path>/before/Lights_50/run1 --after <actual-evidence-path>/after/Lights_50/run1 --threshold 3
```

`bench_compare.py` 对目录默认取一个 run，必须逐 run 和总报告核对，不把单次数字当 3 次汇总。metadata 的 commit 差异预期存在，其他不可比较项必须解释。

比较 frame / render CPU（含准备+构图）、pass GPU、p50/p95、进程内存与受支持的 GPU 内存分列、可用 render counters。超过 3% 是调查门槛而非自动容许；重复出现且超 baseline 噪声的 regression 必须修正或用户明确批准。不要用 GPU 主导的大灯光场景掩盖 CPU 构图开销，至少再有低负载 HelloMaterial 的 prepare/build 对照。

## 8. 完成报告格式

```text
Checkpoint:
Goal and boundary:
Code/test files by responsibility:
Removed plumbing / retained bridge:
Commands, exit codes, test counts:
Runtime evidence paths / comparison:
Owner / allocation / replacement / retirement:
CPU / GPU / memory before-after:
Passed matrix IDs:
Unverified / blocked / deviation:
Next checkpoint and first entry point:
```

某行只完成单测而缺 runtime/perf 时状态最多 `in_progress`。C1–C4 可以有明确归后阶段的非绿矩阵项，但不能改其 expected 或删除它来过门禁。
