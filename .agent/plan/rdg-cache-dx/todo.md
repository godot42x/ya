# 当前待办

> 2026-10-12 任务归属校正；历史验证见 progress，不代表本轮重跑。

## 保留成果

- [x] P0：Scene skinning SSBO 生命周期收敛（历史）。
- [x] P1：registry trace / imported dump / owner 表与 P2 决策（历史）。
- [x] P2：import owner key / owner 提供 image+view / transient 池淘汰（历史）。

## 转交，不是完成

- P3：feature 开发体验与 Bloom 样本已转交 `../render-pipeline-composition/` C1–C5；代码未实施，不另开 Bloom-only 切片。
- P4：Forward/Deferred/Shadow 混合资源职责拆分转交同线 C1–C4；不再单独按旧容器名称推进。
- 多 View runtime 历史未验证项由新线 M22 接续，不能据 scope 单测标 runtime 通过。

## 本线剩余：P5

- [ ] 核对新组合层当前 owner、灯光 shader 改动和 Slang reflection 实际支持字段。
- [ ] `slang_gen_header.py` 生成 entry descriptor set/binding/type/count/stage 信息，不从名称猜。
- [ ] Bloom 与一条完整 pipeline 改用生成 layout；删除所选路径手写字面量，不另造 schema。
- [ ] shader binding 改变后 C++ 生成物随动，一致性由编译/测试暴露。
- [ ] `xmake ya-shader`、Render3D、双管线相关画面对照和当前 allocation/flush 基线；代码与计划同 checkpoint 记录。

P5 不依赖整个组合层全部完工，但涉及相同 feature 的修改必须先明确文件归属和新入口，不能并行重写同一 layout。
