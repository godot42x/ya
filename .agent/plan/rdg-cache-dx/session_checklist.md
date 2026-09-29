# 每轮开工 / 收尾

## 开工

- 读本目录 plan.md 的当前目标、边界和本 phase 验收；读 progress.md 的未决项。
- 只进入当前 phase 所需的主 skill；核对目标文件、调用链与当前分支状态。
- 先确认该 phase 的用户可见 DX 问题与证据；不要仅凭“有两层对象”推断重复 allocation。
- 明确本轮单一可验收目标及边界，再实施；普通 feature 路径要记录实际变更触及的职责。
- P2 开工前确认 progress.md 已有 P1 的 owner 表与明确决策；没有决策不开工。
- P4 开工前核对 render-view-family 当前状态，避免与其 `FrameRecording` 改名批次交叉。

## 资源生命周期审计

- 对每项 GPU 资源分别记录 owner、稳定身份/generation、backing allocation、graph binding、lease/retained 生命周期、失效条件和退役路径。
- 明确区分 wrapper/slice/handle 与物理 allocation；检查实际 allocation/replacement/retirement 计数。
- 验证录制期引用的 GPU resource、image view、descriptor 数据至少活到 queue submit 完成。
- 缓存改动需对比 warmup 后稳定态 flush 与 allocation 基线，并排除 resize、首次创建、退出 flushAll。

## Pipeline contract / feature 扩展

- 从 feature/pipeline 入口追到 pass 组合、graph 读写声明、descriptor/buffer/texture、vertex declaration 和调用方必填输入。
- shader-facing layout（含 descriptor set/binding）以 Slang 生成物为事实源；不复制成手写 schema。生成物改动走 `xmake ya-shader`，不手改 `Generated/*`。
- 记录普通 feature 实际改动的文件和职责；若需修改 executor、registry 或无关缓存，先解释为何这是必要扩展点。
- 对 pipeline 概览标明每项信息的来源；缺失信息显式暴露，不按命名猜测。

## 收尾

- 检查 diff 仅含当前用户目标涉及的计划/代码文件；保留并识别工作区已有的无关改动。
- 跑与改动匹配的测试和诊断；清楚区分本轮新验证与历史基线。
- 更新 progress.md 和 feature_matrix.json 的状态、证据、保留项、未完成项及偏离。
- 检查计划中的结论能映射到实际职责与验收项；未闭环前不拆成进度提交。
