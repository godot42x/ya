# Worker 执行指南

## 1. 开始任务

1. 读根目录 `AGENTS.md`。
2. 只读当前任务对应的一个主 skill：graph/runtime 用 `render-arch`，GPU asset/upload 用 `resource-system`，构建失败用 `ya-build`。
3. 读本目录 `plan.md` 的目标架构、`todo.md` 中当前任务和 `progress.md` 最新记录。
4. 执行 `git status --short`，记录并避开不属于本任务的工作区修改。
5. 检查任务依赖是否全部 `[x]`；否则不要跳做后置任务。
6. 将任务标为 `[-]`，在 `progress.md` 写开始时间、代码事实和预计提交边界。

## 2. 实现纪律

- 每次只解决一个任务 ID，或 `todo.md` 明确允许合并的一组任务。
- 先添加/调整契约测试，再迁一个真实 consumer；不要一次迁完全部 Stage。
- owner 搬迁提交不得顺手重写 draw loop、shader 或 material 行为。
- common abstraction 必须至少有 Deferred/Forward 两个真实消费者；只有一个 consumer 时放在对应 pipeline 模块。
- Graph setup 和 execute 必须使用同一 pass parameters；不得从 Stage 成员补读资源。
- execute callback 不得创建/替换资源、`waitIdle()`、查询 App/scene/service 或手写未声明 transition。
- graph handle、resolved pointer 和 frame-local draw view 不得跨帧保存。
- 生成文件只读；shader-facing 类型只消费生成头。

## 3. 调查与停止条件

出现以下情况时停止写代码，先完成一个 `progress.md` 调查记录：

- 计划假设与实际 owner/lifetime 不一致。
- 一个资源同时被 Deferred、Forward、offscreen 或 editor 以不同 completion boundary 使用。
- 需要新增 Vulkan-only 公共 API。
- 为完成任务必须同时修改 resource API、shader layout 和两条 pipeline。
- 自动化基线不能稳定复现，无法判断视觉行为是否变化。
- 现有局部 executor 实际承担独立 submit/job lifetime，不能安全并入 world executor。

调查记录必须给出代码位置、真实调用链、可选方案、推荐方案和需要修改的任务依赖。
只有涉及产品行为/公共 API 取舍或无法依据代码判断时才请求用户决定。

## 4. 测试和提交

每次提交前强制运行：

```bash
make test
make b t=HelloMaterial
```

然后运行 `todo.md` 指定的 editor smoke。没有指定时，至少运行受影响 pipeline 的 baseline；
共享 graph/resource/lifetime 修改运行 Deferred、Forward、pipeline switch 和 shutdown/readback。

默认 `--exit-after-frame=1500`。记录：

- 命令和 exit code
- 测试通过数量
- 日志路径及 `Validation Error` / `VUID-` / `[Error]` 搜索结果
- 截图路径、固定机位和 hash（视觉任务）

测试失败时不提交。确认是无关既有失败时，记录完整证据并寻找不掩盖问题的针对性验证；
不能以“看起来无关”为由跳过默认 gate。

提交格式遵循 `[module] message`。提交只 stage 当前任务文件，不包含用户的其他修改。

## 5. 完成任务

1. 检查 diff 是否满足任务的每条验收标准和禁止项。
2. 更新 `todo.md` 为 `[x]`。
3. 在 `progress.md` 写实现摘要、关键决策、测试 artifact、commit hash 和下一可执行任务。
4. 若产生稳定规则，更新对应 skill；若只是历史故障，更新 memory。
5. 再次检查 `git status --short`，确认没有遗漏本任务文件或误纳无关文件。

## 6. Review 清单

- 顶层是否比改动前更容易读出执行顺序？
- 本帧参与 GPU dependency 的资源是否都有 graph handle？
- physical owner 是否唯一且 completion boundary 明确？
- resize 是否通过 desc change + safe replacement？
- setup 与 execute 是否引用同一 params？
- descriptor 实际绑定是否能追溯到 pass declaration？
- transient buffer lifetime 是否来自最终 compiled order，而不是 pass 插入顺序？
- physical slot 数/bytes 是否实际小于可复用的 logical buffer 数/bytes，diagnostics 是否能证明？
- alias identity 切换是否有 barrier/state reset，且同一时刻不会 resolve 两个重叠 logical lifetime？
- CPU 在 execute 前预写的数据是否使用独立 arena slice，而不是错误参与 transient alias？
- per-flight upload arena 是否只在对应 GPU completion 后 reset/reuse？
- 是否新增了仅转发的 helper/facade 或只有一个消费者的过早公共抽象？
- 是否保留了资产 Texture、material cache、PSO 与 frame graph resource 的正确边界？
