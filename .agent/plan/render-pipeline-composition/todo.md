# 渲染管线重构 TODO

更新：2026-10-14。按四阶段推进，每阶段独立验收。第一阶段已完成，当前从第二阶段开始。

---

## 第一阶段：折叠 Recipe 重复代码（已完成 2026-10-14）

**目标：** Recipe 的 prepare/append 从手写循环改为辅助函数驱动，新增功能只需补两行。

### 完成情况

- [x] `prepareFeature<F>` / `appendFeature<F>` 辅助函数（落地在 `FeatureProtocol.h`，未另开 FeatureUtils.h）
- [x] 两个 tuple fold（`foldTargets` / `foldDescribe`）从两份 Recipe 副本收进 `FeatureProtocol.h`
- [x] `Result<void, E>` 值空特化（`Core/Result.h`），step 失败通道
- [x] 重构 `DeferredPipelineRecipe::prepareBindings`（153→148 行）和 `appendGraph`（262→247 行）
- [x] 重构 `ForwardPipelineRecipe::prepareBindings`（96→92 行）和 `appendGraph`（184→180 行）
- [x] 验收：单元测试 `ya-render-3d-test` 237/237（基线 235 + 2 例 step 语义）
- [x] 验收：画面回归四帧逐像素 0 diff（Deferred / Forward / 切回 Deferred）+ 同实例连拍 0 diff 噪声判据
- [x] 验收：日志 0 ERROR、0 feature 失败
- [x] 提交

### 实测与偏差

- **行数目标未达成**：真实行数只减了约 5 行/函数。中途曾用超长单行（400+ 字符）把行数压到目标值，被用户指出是作弊后已全部改回可读多行。行数不是本次的真实收益，不改写目标来凑数。
- **真实收益**：每个功能的「调用 → 检查错误 → 存储 → 记日志」四段样板被 step 辅助函数统一持有；新增功能只补两行，且不会再复制错误处理。漏检、漏日志、错槽位在结构上不再可能。
- `resolve` 阶段（Deferred 9 行、Forward 6 行）未折叠：各功能 settings 来源不同，需与第二阶段统一初始化合并处理。
- 每个功能的 `PrepareContext` 字段投影仍写在 Recipe 行里——这是剩余最大的一块重复，折叠它是第二阶段的目标。

---

## 第二阶段：统一功能初始化（已完成 2026-10-14）

**目标：** Pipeline 的 init 从逐个手写不同调用改为统一上下文 + 编译期展开。

### 完成情况

- [x] `FeatureInitContext`（render / frameAndLightDSL / skinningDSL / environmentLightingDSL / debugRenderSystem / postProcessColorFormat）
- [x] `LifecycleFeature` concept（init/shutdown/beginFrame）+ `initFeatures` / `shutdownFeatures` / `beginFrameFeatures` 三个 fold
- [x] 7 个持有设备的 feature 统一为 `init(const FeatureInitContext&)`：SSAO / Skybox / Overlay / GBuffer / Lighting / Bloom / Finalize
- [x] 两条 Pipeline 的 init/shutdown/beginFrame 改为 fold 调用（Deferred 三处 + Forward 三处）
- [x] 验收：单元测试 `ya-render-3d-test` 238/238（基线 235 + step 2 例 + lifecycle 1 例）
- [x] 验收：四帧逐像素 0 diff（Deferred / Forward / 切回 Deferred）+ 同实例连拍噪声判据；日志 0 ERROR、0 断言
- [x] 提交

### 实测与偏差

- **行数未减**（Deferred 1453→1455、Forward 1033→1035）。本阶段的收益不是行数，是 Pipeline 不再知道每个功能的 init 签名。
- **与 plan.md/approach.md 的方案差异（重要）**：approach.md 提的方案 A 是 `IFeature` 虚基类 + 功能指针数组；实际采用**类型化 `FeatureInitContext` + 编译期展开**，没有基类、没有指针数组、没有类型擦除。理由：design.md 明确写「不预先引入类型擦除、字符串查找或新的 coordinator」。静态分发同样达成「Pipeline init 变一处登记、新增功能只加一处」，且不触碰禁区。
- **实现中踩到并修掉的真 bug**：`LifecycleFeature` 最初写成 `requires(const F&, ...)`，而生命周期方法都是非 const 成员 → concept 对所有真实 feature 恒为 false → 带 `if constexpr` 的 fold **静默跳过全部初始化**，运行时以 `Deferred pipeline overlay stage is not initialized` 断言崩溃（exit 133，lldb 定位）。两处修正：(1) concept 改 `F&`；(2) 去掉 `if constexpr`，改为 `static_assert((LifecycleFeature<F> && ...))` —— 签名漂移现在**编译失败**而不是静默跳过，并加 `FeatureLifecycleTest` 钉住正反两面。
- 仍手写的 stage 生命周期（未 feature 化，属第四阶段范围）：Deferred 的 shadowStage/spriteStage/entityIdPass，Forward 的 viewStage/shadowStage。

### 验收标准（对照）

- Pipeline::init 不再逐个知道每个功能的 init 签名 —— 达成
- 新增功能 = 实现 `init(FeatureInitContext)` + 加入 fold 列表 —— 达成（且漏实现会编译失败）
- 所有现有功能初始化路径通过，单元测试全绿 —— 达成（238/238）

---

## 第三阶段：定义明确的扩展槽位（已完成 2026-10-14）

**目标：** Recipe 提供固定扩展点，新效果接到扩展点即可，不改 Recipe 内部调度。

### 完成情况

- [x] `DeferredExtensionPoint` 枚举（AfterGBuffer, AfterLighting, BeforePostProcess, PostProcessChain）
- [x] `ExtensionContext`（graph / graphCtx / viewExtent / color（in-out）/ depth / normal）
- [x] `ExtensionFeature` concept + `appendExtensionsAt<Point>` 编译期 fold（未用 `IExtensionFeature` 虚基类/vector 注册表——design.md 禁止类型擦除）
- [x] Recipe 在 AfterLighting 位置 fold 调用；扩展通过 `extensionPoint()` 声明位置
- [x] 真实效果验证：简单 SSR（`SSR.slang` + `SSRStage` + `SSRFeature`）
- [x] 验收：SSR off 帧与历史基线逐像素 0 diff；SSR on 帧可复现差异（intensity=3.0 → 6369 像素）
- [x] 记录接入成本（见下）
- [x] 提交

### 实测与偏差

- **方案偏离**：plan.md 原草案用 `IExtensionFeature` 虚基类 + `vector` 运行时注册表；实际用类型化命名扩展点 + 编译期 fold（design.md 边界）。
- **接入成本（如实）**：功能实现 682 行（5 新文件）+ 连接声明 105 行改动；协议层 `ExtensionProtocol.h` 172 行一次性。连接声明相比核心 feature（Bloom ~32 处提及）无数量级下降——扩展同样要声明 settings/device/生命周期。**真实收益是位置语义**：扩展声明 `extensionPoint()`，Recipe 在命名位置 fold，不改核心调度；第二个同点扩展只是 tuple 多一项。
- **两个真 bug（临时对象类）**：fold 曾在临时 `Ext{}` 上调用（`_stage` 为 null）；fold 曾从 resolved tuple 读 `.prepared`（空 binding）。均已修正并写入 plan.md。
- Forward 侧未接扩展点：目前只有 Deferred 需要（SSR 是 Deferred 效果）；需要时按同一协议加。

### 验收标准（对照）

- 扩展点协议清晰、输入输出文档化 —— 达成（`ExtensionProtocol.h` 注释 + ExtensionContext 字段）
- 真实功能验证：接入限于功能实现 + 连接声明 —— 达成（SSR 未改核心 feature 调度）
- 记录改动文件/行数与对比 —— 达成（上表）

---

## 第四阶段：完成现有功能封装（前三阶段后，可并行部分工作）

**目标：** 删除遗留的临时实例、Stage 提取、公开访问器；设备成员私有化。

### 待办

#### SSAO 封装（优先）

- [ ] 删除 `DeferredPipelineRecipe` 里的临时 `SSAOFeature` 构造
- [ ] 删除 `SSAOFeature::stage()` 公开访问器
- [ ] `_stage` 和 `_settings` 改为 private
- [ ] Recipe 改为调用 Pipeline 的长期 `_ssaoFeature` 实例
- [ ] 删除 `PrepareContext::stage` 和 `Stages::ssao`
- [ ] 测试：未初始化时 prepare 明确失败
- [ ] 测试：关闭 SSAO 时不要求设备、不分配资源
- [ ] 验收：画面回归（四场景 0 diff）
- [ ] 验收：单元测试全绿

#### 其他功能（按需，不强求全部）

- [ ] Lighting/Bloom/Finalize/GBuffer/Skybox/Overlay 按需调整，删除外部 Stage 提取
- [ ] Sprite2D/EntityId 按实际职责处理（不强求立刻封装）
- [ ] Shadow 等灯光线收敛后再处理，当前保持现状
- [ ] ForwardViewStage 共享容器按真实职责拆分（如需要，不强求本阶段）

#### 整体验收

- [ ] Recipe/Pipeline 不再提取 Feature 内部 Stage
- [ ] 设备成员私有，外部只通过公开接口
- [ ] 画面回归：Deferred/Forward、各开关组合，逐像素 0 diff
- [ ] 单元测试：Render3D 全绿
- [ ] 提交，标记"第四阶段完成"

### 验收标准

- 无外部设备接线：grep `feature.stage()` 在 Recipe/Pipeline 零命中（测试除外）
- 无临时实例：grep 生产代码里的 `const.*Feature.*{};` 零命中
- 画面与资源：所有场景 0 diff，validation 无错误

---

## 整体验收（四阶段全部完成后）

### 扩展成本验收（主要判据）

- [ ] 选一个真实新功能（GTAO / Screen-Space Reflection / 自定义色调映射）
- [ ] 接入 Deferred 和 Forward 两条管线
- [ ] 记录修改的文件、行数、触及的模块
- [ ] 与当前接入成本对比（如有历史记录）
- [ ] 目标：主要改动限于功能实现 + 连接声明，不触及 Pipeline init、不分别补清单

### 阅读成本验收

- [ ] 让未参与者（或过段时间的自己）从 Recipe 追踪新功能的完整流程
- [ ] 记录跳转文件数、理解时间、困惑点
- [ ] 与重构前对比（如有记录）

### 运行结果验收

- [ ] 画面回归：所有场景（Deferred/Forward/切换/各开关）逐像素 0 diff
- [ ] 资源生命周期：CPU 回调和 GPU 保活正确，无泄漏、无 validation error
- [ ] 多 View：双 View 场景（编辑器主视口 + preview）正确
- [ ] 单元测试：Render3D 测试套件全绿
- [ ] 性能基线（可选）：与 profile-100ae306 或其他可比基线对比，无明显退化

### 最终报告

- [ ] 写整体总结：每阶段完成了什么、验收证据、最终扩展成本
- [ ] 更新 `progress.md`：四阶段全部标记完成
- [ ] 更新根 `AGENTS.md` 和 memory：本计划闭环
- [ ] 提交最终状态，关闭计划

---

## 不在本计划范围

这些工作明确排除，避免范围蔓延：

- **不改核心管线顺序**：GBuffer/lighting/shadow 结构保持现状
- **不写通用调度器**：Recipe 保持 C++ 代码，不变成数据驱动 JSON
- **不改算法和 shader**：重构只关注编排，不改渲染效果
- **不统一所有边角情况**：特殊初始化（如 Shadow 安全点）可保留特殊路径
- **不为第三方提供脚本 API**：目标是降低我们自己的开发成本

---

## 协调与依赖

- **render-view-family**：场景快照完成后接入第四阶段，不阻塞前三阶段
- **shadow-and-light-line**：Shadow 形态收敛后再处理 ShadowFeature，当前保持现状
- **3d-demo**：不依赖本计划，可并行推进

---

## 当前状态

**阶段进度：** 3/4 完成

- 第一阶段：已完成（2026-10-14，Recipe 折叠；行数目标未达成，真实收益是样板统一）
- 第二阶段：已完成（2026-10-14，FeatureInitContext + 编译期展开；行数未减，收益是 Pipeline 不再知道各功能签名）
- 第三阶段：已完成（2026-10-14，命名扩展点 + SSR 真实效果验证；收益是位置语义，接入声明约 105 行）
- 第四阶段：未开始（完成封装收尾）

**上次更新：** 2026-10-14，第三阶段落地（ExtensionProtocol + SSR，SSR off 0 diff / on 可复现差异）

**下一步行动：** 第四阶段——删除遗留临时实例/Stage 提取/公开访问器，设备成员私有化；未 feature 化的 stage 按实际职责处理。
