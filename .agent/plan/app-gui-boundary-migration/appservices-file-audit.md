# Phase A2 — Framework/AppServices File-Level Owner Audit

> 更新时间：2026-08-14
> 作用：确认 Engine/Source/Framework/AppServices 下的文件不是一个单独成立的共享能力根，而是几类 render/runtime contract 与配置类型的临时混放桶。

## 1. 审计结论先说

Framework/AppServices 这个名字会误导人以为它属于 App 主链，但实际文件内容并不支持这个结论：

- ShadowSettings、PostProcessingState 是 render/runtime 配置；
- AppAutomationShadowOverrides 是 render automation override 数据；
- RuntimeServices 虽然名字带 Services，但合同内容是 IRenderRuntimeHostServices，仍然是 render/runtime 消费侧合同；
- 整个目录没有一个文件可以被解释为“共享 App/Kernel 自己的 owner 类型”。

因此它的正确处理方式不是“留在 AppServices”，也不是“顺手搬到 App/Kernel”，而是：

1. 先承认这是一个待拆桶；
2. Batch 1 暂不动物理归属，只改消费者接回新的 App 与 GUI/Host 路径；
3. Batch 2 再按 render/runtime 真 owner 把它拆散。

## 2. 判定规则

| 分类 | 含义 | 迁移动作 |
|---|---|---|
| shared render/runtime contract | render runtime 直接消费的窄合同 / 配置 | 回到 Render/Runtime（命名待定） |
| shared app/kernel contract | 属于无窗口主链自身的 owner / service registry | 才允许进 App/Kernel |
| mixed / split candidate | 文件内部命名或实现含混，需要先拆 registry 与 payload | Batch 2 再拆，Batch 1 不强搬 |

## 3. 文件级审计表

| 文件 | 当前事实 | 分类 | 未来 owner / 分支 | Batch 动作 |
|---|---|---|---|---|
| ShadowSettings.h | 被 RenderRuntime、shadow pipeline、Host render state、测试直接消费；内容完全是 shadow runtime 配置与 helper | shared render/runtime contract | Render/Runtime 的 shadow config / common settings | Batch 1 不动；Batch 2 移出 AppServices |
| PostProcessingState.h | 被 RenderRuntime、deferred/basic/bloom/postprocess pipeline 直接消费；没有 App 主链语义 | shared render/runtime contract | Render/Runtime 的 postprocess config | Batch 1 不动 |
| AppAutomation.h | 只有 AppAutomationShadowOverrides；本质是叠加到 ShadowSettings 上的 render automation override 数据 | shared render/runtime contract | Render/Runtime 的 automation/config override leaf | Batch 1 不动；后续最好去掉 AppServices 语义命名 |
| RuntimeServices.h/.cpp | 定义 IRenderRuntimeHostServices、IOffscreenTaskScheduler 与 RuntimeServices::set/getRenderRuntimeHost；名字像通用 services，但实际 payload 全是 render/runtime host contract（window、shadow、offscreen job） | mixed / split candidate | 优先归 Render/Runtime host contract；若保留通用 registry，则 registry 壳再单独评估是否放入 App/Kernel | Batch 1 不动；Batch 2 先拆“render-specific contract”与“registry 壳”再定最终路径 |

## 4. 为什么它不该回到 App/Kernel

虽然目录名叫 AppServices，但现有文件不符合 App/Kernel charter：

1. ShadowSettings、PostProcessingState 是 render pipeline 配置，不是主循环或 control plane。
2. IRenderRuntimeHostServices 直接暴露主 presentation window、shadow settings、offscreen GPU job queue，这些都不是 windowless App/Kernel 自己的能力。
3. IOffscreenTaskScheduler 依赖 ICommandBuffer 与 OffscreenJobState，明显是 render-side task contract。
4. 如果把这整桶放回 App/Kernel，会再次把共享 App 主链和 render/runtime 语义搅在一起。

因此这里真正需要的是“按真实消费 owner 拆桶”，不是再造一个泛化 AppServices 名字。

## 5. 对 target 与 include root 的直接结论

当前 ya-app-services target 只是过渡产物，不是终局命名。它的长期去向只可能是下面两类之一：

1. 收口为 render/runtime scoped target。
2. 按稳定职责继续拆小：render config 与 render host contract 分开。

无论选哪条，都不该继续叫 AppServices，也不该整体搬去 Game。

## 6. 对 Batch 1 / Batch 2 的约束

### Batch 1

- 不动 Framework/AppServices 的物理位置。
- 允许的唯一动作是让 Product/Host 与其它消费者接回新的 App 与 GUI/Host 公开头。
- 不要在这一批顺手改 ya-app-services target 名，避免把目录迁移和 render contract 重构混成一件事。

### Batch 2

1. 先把 RuntimeServices.h 里的 registry 壳与 render-specific contract 拆开评估。
2. 再把 ShadowSettings、PostProcessingState、AppAutomationShadowOverrides 一次性迁回 render/runtime owner。
3. 迁移时同步改 include root，禁止留下新的 AppServices spellings。
4. 若届时发现 registry 壳仍然强绑定 render/runtime，就整组留在 render/runtime 分支，不要硬塞入 App/Kernel。

## 7. 收口判断

完成这份审计后，可以把 Framework/AppServices 的口径固定下来：

- 它不是共享 App 主链；
- 它不是应用壳；
- 它是 render/runtime contract 与配置的暂存桶；
- 下一阶段该做的是“拆散归回真实 owner”，而不是继续扩写 AppServices 名字。
