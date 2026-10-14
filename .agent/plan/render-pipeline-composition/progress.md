# 当前进展

更新：2026-10-14。这是当前事实摘要，不是逐次工作日志。

## 已完成的结构改动

Forward/Deferred 已有各自 Recipe，旧 orchestrator、集中 pass context 和功能资源大包已退出。功能声明资源，Recipe 连接功能；共享 frame/light/skinning 输入保留在专门的提供者里。

Bloom、EntityId、Finalize、Shadow 的组合实现被两条管线共用。设置与持久资源准备移到录制前，输出按句柄读取，构图失败时不执行该 family 的部分结果。

设备归位已覆盖 SSAO、Skybox、Overlay、Bloom、Finalize、GBuffer、Lighting。共用的 overlay 与 postprocess 容器已按功能拆开。

这些改动有历史单测与运行时证据，不需要重做旧入口迁移。

## 当前最重要的缺口

设备所有者与调用对象分离：Pipeline 的长期 Feature 持设备，Recipe 的临时 Feature 使用外部传入的设备。初始化、准备、执行仍有多份逐功能登记。这就是下一步从 SSAO 开始修正的原因。

已经存在 tuple 和资源汇总遍历，但没有因此自动完成所有准备和调用。阶段输入分型未完成；扩展测试手工调用测试功能，没有完整进入真实 Recipe 的申请、准备、构图和输出路径。

多 View 实跑、完整资源变更和可比性能证据仍缺。画面不变和设备成员减少都不能替代编排目标验收。

## 不再使用的结论

- “只剩 Shadow”：还存在 Sprite2DStage、EntityIdPass 和 ForwardViewStage。
- “迁 ShadowStage 必须迁阴影图资源”：Stage 不拥有 ShadowMapResources，安全点只限制操作时机。
- “Feature 必须无状态”：长期 Feature 可以持设备和权威设置；本帧确定的参数应独立保存，不能从另一个实例读默认值。
- “tuple 已完成所以扩展已验收”：遗漏方法检查不等于遗漏阶段调用检查，真实扩展测试仍未完成。

验收表中的 M21、M26 已恢复 in_progress，原标准保留。M28 设备封装仍未完成。M29 只记录下一 SSAO 任务，状态 todo、没有实现证据。

## 最近一次更新

2026-10-14：第二阶段落地——统一功能初始化。`FeatureInitContext`（6 个字段）+ `LifecycleFeature` concept + `initFeatures`/`shutdownFeatures`/`beginFrameFeatures` 三个编译期 fold 进 `FeatureProtocol.h`；7 个持设备的 feature 统一为 `init(const FeatureInitContext&)`；两条 Pipeline 的 init/shutdown/beginFrame 改为 fold 调用。**行数未减**（Deferred 1453→1455、Forward 1033→1035）——收益是 Pipeline 不再知道每个功能的 init 签名。**与 approach.md 方案 A 的偏离**：未用 `IFeature` 虚基类数组（design.md 禁止预先引入类型擦除），改用类型化上下文 + 编译期展开。**踩到并修掉的真 bug**：`LifecycleFeature` 最初写 `const F&` 而生命周期方法非 const → concept 恒 false → 带 `if constexpr` 的 fold 静默跳过全部初始化 → 运行时断言崩溃（exit 133）。修正为 `F&` + `static_assert` 强制（签名漂移现在编译失败），并加 `FeatureLifecycleTest` 钉住。验证：`ya-render-3d-test` 238/238；四帧逐像素 0 diff + 同实例连拍噪声判据；日志 0 ERROR、0 断言。

2026-10-14：第一阶段落地——Recipe 折叠。`prepareFeature` / `appendFeature` 与两个 tuple fold 进 `FeatureProtocol.h`，`Result<void, E>` 进 `Core/Result.h`。真实行数：Deferred prepare 153→148、append 262→247，Forward prepare 96→92、append 184→180——**行数目标未达成**（中途用超长单行压行数被用户指出是作弊，已改回可读多行）。真实收益是每个功能的「调用→查错→存储→日志」样板被统一持有、新增功能只补两行。验证：`ya-render-3d-test` 237/237（基线 235 + 2 例 step 语义），四帧逐像素 0 diff（Deferred / Forward / 切回 Deferred）+ 同实例连拍 0 diff 噪声判据，日志 0 ERROR。未折叠 `resolve` 行与各功能 `PrepareContext` 字段投影，留给第二阶段。

2026-10-14：重写当前计划文档，去掉批次流水账与过期草案；随后修正范围表达。整体目标始终是两条渲染管线的连接、功能封装、统一登记与驱动，而非 SSAO 专项。当前先用 SSAO 验证封装，随后用异构功能验证重复登记删除，再覆盖完整管线和整体验收。

旧计划、逐次日志、截图/测试路径及提交记录完整保存在 [历史目录](history/README.md)。本次之后，进展只记录完成内容、验证结果、保留问题和下一工作，不再复制整份计划或累计“完成百分比”。
