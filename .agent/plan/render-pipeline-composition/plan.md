# 渲染管线重构计划

更新：2026-10-14。核心管线（Deferred/Forward/Clustered/Forward+）固定优化，扩展点清晰可控。目标是降低新增效果的接入成本，不追求任意重组管线结构。

## 目标：降低编排复杂度，明确扩展边界

### 我们要解决什么问题

**当前痛点：** 增加一个渲染效果（如新 AO 算法、screen-space reflection、自定义后处理）需要修改 10+ 处代码，分散在初始化、准备、执行、销毁等多个阶段，必须理解整条管线才能插入一个局部功能。

**目标：** 新增效果主要改两处——功能实现 + 管线连接声明，不再在生命周期、资源准备、执行调度等机械流程中重复登记。核心管线结构（GBuffer、lighting、shadow 顺序）保持稳定和优化，灵活性给在后处理和视觉效果层。

### 业界对标与边界

参考 UE5 SceneViewExtension、Unity HDRP Custom Pass、Godot 4 Compositor 的经验：

- **固定核心 + 扩展槽位** 胜过完全任意编排（后者维护成本高、性能难优化、实际需求少）
- **后处理和视觉效果层灵活**，GBuffer/lighting/shadow 等核心流程保持硬编码和深度优化
- **数据驱动的边界**：设置和参数可配置，执行逻辑保持代码（Recipe 不需要变成 JSON）
- **功能封装**：上层只关心输入输出，不知道内部有几个 Stage、怎么初始化

**我们的定位：** 引擎开发者，不是引擎用户。目标不是"给第三方提供脚本化管线 API"，而是"让我们自己开发和维护渲染功能时，编排成本更低、代码更清楚"。

### 成功标准

**主要判据：** 增加一个新后处理效果或视觉功能时，从改 20 处降到改 2-3 处（功能实现 + Recipe 连接 + 可选的扩展点注册）。

**不是成功标准的：**
- 所有 Stage 都变成 Feature 成员
- Recipe 文件行数减半
- 能够任意重排 GBuffer/lighting/shadow 的顺序
- 第三方能通过脚本添加管线

## 整体工作路线

分四个阶段，每个阶段独立验收、独立交付价值：

### 第一阶段：折叠 Recipe 重复代码（立刻可做，低风险）

**现状：** `DeferredPipelineRecipe::prepareBindings`（~200 行）和 `appendGraph`（~350 行）是 8-10 个功能的手写调用循环，每个功能重复"调用 → 错误处理 → 存储结果"模式。

**目标：** 提取 `prepareFeature` / `appendFeature` 辅助函数，让 Recipe 主体变成功能清单 + 一行调用，不再手写重复的错误处理和存储代码。

**改动：**
```cpp
// 当前模式（重复 8-10 次）：
auto ssaoResult = stages.ssao->prepareBindings(resolved.ssao, ctx);
if (ssaoResult.isErr()) return Err(ssaoResult.error());
prepared.ssao = ssaoResult.value();

// 改为辅助函数：
if (auto err = prepareFeature(*stages.ssao, resolved.ssao, ctx, prepared.ssao); err.isErr())
    return err;
```

**验收标准：**
- Recipe 的 `prepareBindings` 从 ~200 行降到 ~50 行
- `appendGraph` 从 ~350 行降到 ~100 行
- 逻辑完全等价，画面逐像素 0 diff
- 新增功能时 Recipe 只需补两行（prepare 一行 + append 一行）

**文件范围：** `DeferredPipelineRecipe.cpp`、`ForwardPipelineRecipe.cpp`（如已有），辅助函数可放在 `FeatureProtocol.h` 或单独工具文件。

**时间：** 1-2 天（含测试与验收）

---

### 第二阶段：统一功能初始化（随后，需设计初始化协议）

**现状：** Pipeline 的 `init` 里 10 个功能各有不同签名：
- SSAO: `init(IRender*)`
- Lighting: `init(IRender*, frameAndLightDSL, environmentLightingDSL)`
- GBuffer: `init(IRender*, frameAndLightDSL, ...)`
- Overlay: `init(IRender*, DebugRenderSystem*)`

Pipeline 必须逐个知道每个功能的初始化需求。

**目标：** 设计统一的初始化上下文，让 Pipeline 不再知道每个功能的具体 init 签名；功能自己从上下文里取需要的依赖。

**方案：**
```cpp
struct FeatureInitContext {
    IRender* render;
    stdptr<IDescriptorSetLayout> frameAndLightDSL;
    stdptr<IDescriptorSetLayout> environmentLightingDSL;
    DebugRenderSystem* debugRenderSystem;
    // 其他共享依赖...
};

// 每个 Feature 统一签名，自己决定用哪些字段：
void SSAOFeature::init(const FeatureInitContext& ctx) {
    // 只用 ctx.render
}
void LightingFeature::init(const FeatureInitContext& ctx) {
    // 用 ctx.render + 两个 DSL
}

// Pipeline 的 init 变成表格驱动：
FeatureInitContext ctx{...};
for (auto* feature : {&_ssaoFeature, &_lightFeature, &_gBufferFeature, ...}) {
    feature->init(ctx);
}
```

**验收标准：**
- Pipeline 的 `init` 从逐个手写不同调用改为统一循环
- 新增功能时 Pipeline 只需把它加入功能数组，不再补特殊初始化代码
- 所有功能初始化成功，设备对象与当前实现等价

**改动：** 
- Feature Protocol 加 `init(const FeatureInitContext&)` 要求
- 10 个 Feature 适应新签名
- Pipeline init/shutdown 改为表格驱动

**时间：** 3-5 天（需改所有 Feature，测试初始化路径）

---

### 第三阶段：定义明确的扩展槽位（基于前两阶段成果）

**现状：** 没有明确的"在哪里可以插入新效果"的协议，每个位置都要手动接线。

**目标：** 参考 UE5 SceneViewExtension，在 Recipe 里定义几个固定扩展点，每个扩展点的输入输出清楚，用户实现功能后注册到扩展点即可。

**扩展槽位设计（初步）：**
```cpp
// DeferredPipelineRecipe 提供的扩展点：
enum class DeferredExtensionPoint {
    AfterGBuffer,        // GBuffer 完成后，lighting 前（适合自定义 AO、thickness map）
    AfterLighting,       // 光照完成后，透明物体前（适合 SSR、SSGI）
    BeforePostProcess,   // 场景渲染完成，后处理前（适合自定义 resolve）
    PostProcessChain,    // 后处理链中（适合自定义色调、滤镜）
};

struct ExtensionContext {
    RenderGraph& graph;
    const ViewTargetLease* viewLease;
    RGTextureHandle sceneColor;
    RGTextureHandle depth;
    // 当前扩展点可用的输入...
};

// Feature 可以注册到扩展点：
class CustomSSGIFeature : public IExtensionFeature {
    Result<RGTextureHandle> execute(ExtensionContext& ctx) override;
};

// Recipe 在对应位置调用已注册的扩展：
auto extensionOut = executeExtensions(DeferredExtensionPoint::AfterLighting, ctx);
```

**验收标准：**
- 用一个真实新功能（如 GTAO、简单 SSR）验证扩展点接入
- 接入只需实现功能 + 注册到扩展点，不改 Recipe 内部逻辑
- 记录修改的文件数和行数，与当前接入成本对比

**改动：**
- Recipe 定义扩展点枚举和上下文
- Recipe 在固定位置调用扩展（初期可用简单的 vector 存储已注册功能）
- 实现一个示例扩展功能作为模板

**时间：** 3-5 天（设计扩展协议 + 实现调用机制 + 验证真实功能）

---

### 第四阶段：完成现有功能封装，删除遗留接线（收尾）

**现状：** SSAO/Bloom/Finalize/Lighting/GBuffer/Skybox/Overlay 已持有设备，但仍有部分旧调用模式（临时实例、Stage 提取、公开访问器）。

**目标：** 
- 完成 SSAO 长期实例调用（删除临时实例、Stage 回传、公开 stage()）
- 其余功能按需调整，重点是删除"Pipeline 提取 Stage 再传给 Recipe"的模式
- Sprite2D/EntityId/Shadow 按实际职责处理（不强求一 Feature 一 Stage）

**边界：**
- 不改算法、shader、颜色空间
- 共享容器（如 ForwardViewStage 内部混合 skybox+simple+debug）按真实职责拆分，不硬套 Feature 结构
- Shadow 的三层结构（ShadowMapResources/ShadowStage/ShadowRuntimeState）与灯光线协调，不单方面重构

**验收标准：**
- Recipe/Pipeline 不再提取 Feature 内部 Stage 或操作内部 setter
- 设备成员改为私有，外部只通过公开接口使用
- 画面回归：Deferred/Forward、SSAO on/off、各后处理开关，逐像素 0 diff
- 单元测试全绿

**时间：** 按功能分批，每批 1-2 天；总计 1-2 周

---

## 整体验收：用真实扩展和运行结果证明

完成四个阶段后，用以下证据验收整体目标：

### 1. 扩展成本验收（主要判据）

用一个真实新功能（推荐：GTAO 或 Screen-Space Reflection）接入 Deferred 和 Forward 两条管线：

- **记录修改符号**：改了哪些文件、多少行、触及哪些模块
- **与当前成本对比**：如果有历史记录（如之前接入 SSAO 的 PR），对比修改范围
- **目标：** 主要改动限于功能实现 + Recipe 连接（或扩展点注册），不触及 Pipeline init、不分别补 prepare/append 清单

### 2. 阅读成本验收

让未参与重构的人（或过一段时间后的你自己）从 Recipe 入口追踪该新功能的启用、输入、准备、资源、输出和失败边界：

- 记录需要跳转的文件数、理解所需时间、遇到的困惑点
- 与重构前的同类任务对比（如果有记录）

### 3. 运行结果验收

- **画面回归：** 所有现有场景（Deferred/Forward/切换/各开关组合）逐像素 0 diff
- **资源生命周期：** CPU 回调和 GPU 引用保活正确，无泄漏、无 validation error
- **多 View：** 双 View 场景（编辑器主视口 + preview）正确渲染
- **单元测试：** Render3D 测试套件全绿

### 4. 性能基线（可选）

如果有可比基线（如 profile-100ae306），对比主要场景的帧时间，确保重构没有引入性能退化。性能优化不是本次目标，但不应退化。

---

## 当前状态与下一步

### 已完成

- 第一阶段完成（2026-10-14）：Recipe 的 prepare/append 折叠为表格式调度，`prepareFeature` / `appendFeature` 与两个 tuple fold 收进 `FeatureProtocol.h`；`Result<void, E>` 补上值空特化（`Core/Result.h`）。
- Recipe 集中了主要渲染顺序和数据连接
- 功能自己声明资源需求，两条管线共用部分组合代码
- 七个功能已持有设备对象，初步封装完成

### 第一阶段实测（对照验收标准）

| 项 | 之前 | 之后 |
| --- | --- | --- |
| Deferred `prepareBindings` | 153 行 | 148 行 |
| Deferred `appendGraph` | 262 行 | 247 行 |
| Deferred 整文件 | 617 行 | 573 行 |
| Forward `prepareBindings` | 96 行 | 92 行 |
| Forward `appendGraph` | 184 行 | 180 行 |
| Forward 整文件 | 434 行 | 405 行 |

**行数目标未达成，如实记录。** 中途曾用超长单行（400+ 字符）把行数压到目标值，被用户指出是作弊后已全部改回可读多行形态；上表是改正后的真实数字。

**真正带来的改动不是行数，而是：** 每个功能的「调用 → 检查错误 → 存储 → 记日志」四段样板代码被 `prepareFeature` / `appendFeature` 统一持有。改动前每个功能在 prepare 和 append 各有一份手写错误处理；改动后一行调用即一个功能，漏检错误、漏日志、错用槽位在结构上不再可能。新增功能时 Recipe 补两行（prepare 一行、append 一行），且不会再复制错误处理。

单功能机械行数没有减少的原因：typed `PrepareContext` 的字段构造（4-7 行）随功能数量线性存在，这是连接信息本身，不是重复代码；把它折叠需要第二阶段统一初始化上下文。

验收证据：`ya-render-3d-test` 237/237（基线 235 + 新增 2 例 step 语义）；四帧逐像素 0 diff（Deferred / Forward / 切回 Deferred）；同实例连拍 0 diff 确认截图无噪声；日志 0 ERROR、0 feature 失败。

### 剩余的重复模式（供第二、三阶段参考）

- `resolve` 阶段仍是逐功能手写行（Deferred 9 行、Forward 6 行）。每行的 settings 来源不同（有的按帧构造），折叠要先把 settings 构造也收拢，留给第二阶段统一初始化一起看。
- 两个 Recipe 各自维护一份 feature 实例清单与 Stages 指针表——第二阶段表格驱动要消除的就是这份清单。
- 每个功能的 `PrepareContext` 字段投影（哪些 device / 哪个 binding）目前写在 Recipe 行里。这是第一阶段后最大的一块剩余重复，其折叠正是第二阶段统一初始化的目标。

### 下一步（按四阶段顺序）

**第二阶段：统一功能初始化。** 设计 `FeatureInitContext`，让 Pipeline 的 init/shutdown 从逐功能特殊调用改为统一循环；前置是核对其与第一阶段的 step 行如何共用同一份"功能清单"。

**然后第三、四阶段依次推进。** 每个阶段独立验收、独立提交，不等全部完成。

---

## 不做的事（明确边界）

- **不改核心管线顺序：** GBuffer → lighting → shadow 的结构保持稳定，不追求任意重排
- **不写通用调度器：** Recipe 保持 C++ 代码，不变成数据驱动的 JSON 或 DSL
- **不统一所有初始化：** 如果某个功能的初始化确实特殊（如 Shadow 的安全点资源重建），可以保留特殊路径，不强求万能上下文
- **不改算法和 shader：** 重构只关注编排和调用，不改渲染效果本身
- **不为第三方提供脚本 API：** 目标是降低我们自己的开发成本，不是给外部用户提供可编程管线

---

## 文档与协作

### 计划文件

- `plan.md`（本文件）：整体目标、工作路线、验收标准
- `migration.md`：具体改动文件清单（第四阶段用）
- `validation.md`：验收检查项和证据要求
- `progress.md`：阶段进度记录，完成一个阶段更新一次

### 提交规范

每个阶段完成后一次提交，消息格式：

```
[render/composition] 阶段名：简要说明

- 改动要点 1
- 改动要点 2
- 验收证据（行数、测试、画面）
```

例如：
```
[render/composition] 第一阶段：折叠 Recipe 重复调用

- prepareBindings 从 217 行降到 48 行
- appendGraph 从 364 行降到 96 行
- 提取 prepareFeature/appendFeature 辅助函数到 FeatureUtils.h
- 验收：逻辑等价，Render3D 测试 235/235，四场景逐像素 0 diff
```

### 与其他计划的协调

- **render-view-family：** 场景快照功能完成后接入第四阶段，不阻塞前三阶段
- **shadow-and-light-line：** Shadow 资源形态收敛后再处理 ShadowFeature 封装，当前保持现状
- **3d-demo：** 不依赖本计划，可并行推进

---

## 参考与灵感来源

- **UE5 SceneViewExtension：** 固定扩展槽位的设计
- **Unity HDRP Custom Pass：** 明确的扩展点协议
- **Godot 4 Compositor：** 后处理层的可组合 Effect
- **RenderGraph（UE5/Frostbite）：** 资源依赖自动化，pass 只声明输入输出

关键教训：**不追求完全灵活的任意编排（维护成本高、实际需求少），而是在固定核心管线的基础上，给后处理和视觉效果层提供清晰的扩展能力。**
