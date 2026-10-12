# 当前待办

> 2026-10-12；计划工件已建立，C1–C5 全部未开始。文档落盘不计代码 checkpoint。

## 下一步：C1 单源组合贯通 SSAO → Lighting

入口顺序：`plan.md` §6 C1 → `design.md` §2–6 → `migration.md` §3–4 → `validation.md` V0。

- [ ] 核对灯光线最新 diff / progress，确认 baseline 使用的整合版本；不覆盖 shared resource container 改动。
- [ ] V0：Render3D 测试、Runtime/Editor build、双管线画面/topo、AO/颜色开关、资源 trace 与性能起点。
- [ ] 最小 typed refs / feature concept / resolve storage / seal，真实 heterogeneous 调用与错误测试。
- [ ] `prepareFrameRecord` 返回本次 prepared frame，应用局部持有，family record 显式消费；设置在安全点冻结。
- [ ] SSAO 局部契约贯通 AO requirement、generated UBO、DSL/DS/upload、import、pass 和 OptionalAO。
- [ ] Lighting 消费 OptionalAO 和明确共享输入，graph/CIS 准备局部化；不复制 shared frame/light。
- [ ] legacy bridge 分 prelude/tail，资源声明一次；Forward 过渡 prepared token 接入。记录删除期限。
- [ ] 删除 bUseSSAO 透传、外层 SSAO 分支 / slots / 集中分配；旧 AO/Light graph 路径退出。
- [ ] C1 完整证据与 matrix 状态，列保留桥接 / 未验证项；满足目标才允许单一 checkpoint 提交。

## 后续目标

- [ ] **C2**：多 pass 颜色贡献 + 必需 Finalize + 单源输出收集；Bloom 是协议样本而非架构主题。
- [ ] **C3**：完整 Deferred recipe；legacy/context/resources bag/集中分配退出，shared inputs 与最新灯光逻辑保留。
- [ ] **C4**：完整 Forward + Shadow 接入 / 生命周期收口；所有迁移 adapter 退出，双 recipe 同协议不同语义。
- [ ] **C5**：真实组合地图诊断、扩展用例、多 View runtime、最终 performance/churn 门禁与 skill 吸收。

## 不得漏掉

- [ ] M22 双管线多 View 实跑；历史 scope 单测不是 runtime 验证。
- [ ] AO/颜色 feature 关闭时无专属资源/bindings/workload/旧图 publication；Finalize 仍执行。
- [ ] 同 backing 续写、空 draw attachment 语义、Shared GPU inputs 不重复上传。
- [ ] 旧灯光 GPU 字段映射、生成类型、预算和现有 shadow 策略用例随迁移保留。
- [ ] prepare/build/compile/execute 失败均不产生部分新 family 发布。
- [ ] 不并行推进 `rdg-cache-dx` 旧 P3/P4 或旧 orchestrator 改名批次。
- [ ] `rdg-cache-dx` P5 未完成前，不宣称 shader layout 单源化已完成。

## 当前阻塞 / 未验证

无架构方向的待确认阻塞；具体 backend 多 View fixture、画面确定性和性能噪声需 V0/C4 实测。工作区灯光线交叉改动需在 C1 前核对归属与整合版本。若实际实现违反 `plan.md` §8，应停止并复审，不臆造下一任务。
