# 渲染管线重构 TODO

更新：2026-10-14。按四阶段推进，每阶段独立验收。当前从第一阶段开始。

---

## 第一阶段：折叠 Recipe 重复代码（当前执行）

**目标：** Recipe 的 prepare/append 从手写循环改为辅助函数驱动，新增功能只需补两行。

### 待办

- [ ] 写 `prepareFeature<F>` 辅助函数（FeatureProtocol.h 或新 FeatureUtils.h）
- [ ] 写 `appendFeature<F>` 辅助函数
- [ ] 重构 `DeferredPipelineRecipe::prepareBindings`，用辅助函数替代手写循环
- [ ] 重构 `DeferredPipelineRecipe::appendGraph`，用辅助函数替代手写循环
- [ ] 如有 ForwardPipelineRecipe，同样重构
- [ ] 验收：逻辑等价测试（单元测试全绿）
- [ ] 验收：画面回归（Deferred/Forward/SSAO on-off/各开关，逐像素 0 diff）
- [ ] 验收：行数统计（prepareBindings 和 appendGraph 各降到多少行）
- [ ] 提交，标记"第一阶段完成"

### 验收标准

- `DeferredPipelineRecipe::prepareBindings` 从 ~200 行降到 ~50 行
- `DeferredPipelineRecipe::appendGraph` 从 ~350 行降到 ~100 行
- 单元测试：`xmake r ya-render-3d-test` 全绿
- 画面：四场景（Deferred/Forward/SSAO on/SSAO off）逐像素 0 diff
- 代码：每个功能的接入从 ~10 行重复代码变成 1-2 行调用

---

## 第二阶段：统一功能初始化（第一阶段后）

**目标：** Pipeline 的 init 从逐个手写不同调用改为统一上下文 + 表格驱动。

### 待办

- [ ] 设计 `FeatureInitContext` 结构（包含所有功能可能需要的初始化依赖）
- [ ] Feature Protocol 加 `init(const FeatureInitContext&)` 要求
- [ ] 10 个 Feature 适应新 init 签名（SSAOFeature, LightingFeature, GBufferFeature, ...）
- [ ] Pipeline 的 init 改为表格驱动（功能数组 + 循环）
- [ ] Pipeline 的 shutdown 同样改为表格驱动
- [ ] 验收：所有功能初始化成功，设备对象与当前等价
- [ ] 验收：新增功能时 Pipeline 只需加入数组，不补特殊 init 代码
- [ ] 提交，标记"第二阶段完成"

### 验收标准

- Pipeline::init 不再逐个知道每个功能的 init 签名
- 新增功能 = 实现 `init(FeatureInitContext)` + 加入 Pipeline 功能数组
- 所有现有功能初始化路径通过，单元测试全绿

---

## 第三阶段：定义明确的扩展槽位（前两阶段后）

**目标：** Recipe 提供固定扩展点，新效果注册到扩展点即可，不改 Recipe 内部。

### 待办

- [ ] 定义 `DeferredExtensionPoint` 枚举（AfterGBuffer, AfterLighting, BeforePostProcess, PostProcessChain）
- [ ] 定义 `ExtensionContext` 结构（每个扩展点可用的输入输出）
- [ ] Recipe 在对应位置调用已注册扩展（初期可用简单 vector）
- [ ] 定义 `IExtensionFeature` 接口或协议
- [ ] 实现一个示例扩展（如 GTAO 或简单 SSR）作为验证
- [ ] 验收：示例扩展接入只需实现功能 + 注册，不改 Recipe 逻辑
- [ ] 记录修改的文件数和行数，与当前接入成本对比
- [ ] ForwardPipelineRecipe 提供对应的扩展点（如需要）
- [ ] 提交，标记"第三阶段完成"

### 验收标准

- 扩展点协议清晰：每个点的输入输出文档化
- 真实功能验证：接入改动限于功能实现 + 扩展点注册
- 对比当前成本：记录改了几个文件、多少行，与无扩展点时对比

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

**阶段进度：** 0/4 完成

- 第一阶段：未开始（下一步）
- 第二阶段：未开始
- 第三阶段：未开始
- 第四阶段：未开始

**上次更新：** 2026-10-14，重新规划四阶段路线

**下一步行动：** 开始第一阶段，写 `prepareFeature` / `appendFeature` 辅助函数。
