# 每轮开工与收尾

## 开工

1. 读根 `AGENTS.md`、本线 plan 当前 checkpoint、todo、progress 未完成项；只按需进入一个主 skill。
2. `git status --short` / 相关 diff / 最近本线 commit；标出非本轮改动，尤其灯光、FrameResourceSet、Shadow、Runtime、Slang。
3. 复述本轮单一验收目标、保留行为、涉及文件与不做项。未经授权不实施计划之外的修复 / rename。
4. C1 先拿 V0；后续核对上一 checkpoint 可构建、可运行，不把历史 188 tests / flush 帧号当新基线。
5. 根据 migration 确认旧符号仍在、最新 owner 和 generated payload；不得按旧文件行号盲搬。
6. 编码前列删除项和迁移桥接：每个 adapter 的 consumers、剩余字段、退出 checkpoint。C3/C4 应真正删除旧入口。
7. 构建/运行使用当前 ya-build；同目录并发用 build lock，切 profile 使用独立 worktree。

## 实施自检

- 该功能只有一次 resolve；申请、bindings、import/pass/publish 使用同一个 invocation 结果。
- feature Inputs / Outputs / Prepared 是局部 typed data，不通过 all-stage context 查任意资源/设置。
- shared frame/light/skinning 真共享，feature-local UBO/DS 不进入 shared provider。
- stable resource key 不依赖 feature 插入序号；graph handle / CPU record / wrapper 不等于 allocation。
- persistent targets/PSO variants 在安全点准备；descriptor/upload 仍由现有 submission 分配。
- execute callbacks 只捕获 immutable data、resolved handles、稳定执行对象/资源；不问 active scene/live ECS/当前设置。
- skipped 合法、required-missing/failed 显式；不能用 missing-target→transient 或 invalid→first-output 掩盖。
- pass draw、Load/Store、Depth、颜色位置、display 编码保持当前语义；UI/present 不进 world recipe。
- GPU 引用/resource view/descriptor/PSO/sampler 按既有机制 retain 到提交完成；CPU prepared 销毁不代替 GPU 保活。
- shader-facing layout 走 Slang 生成物；本线不造第二份 set/binding manifest。

## 收尾

1. 跑本 checkpoint 的行为测试、Render3D 全量、所需 closure/integration/runtime；记录真实命令、退出码、测试数量（filter 0 cases 不算过）。
2. 给出 per-pipeline 画面对照、资源 owner/churn 与所需 performance 证据；没有跑的写未验证，不能泛称通过。
3. 对照 matrix ID 和 checkpoint 删除清单检查实际 diff；已删除旧用例列等价替代测试。
4. `git diff --check`、`git diff --stat` 与非本轮工作区改动；计划 JSON 解析、ID/phase/链接一致性。
5. 更新 progress / todo / matrix：保留、未完成、偏离、阻塞、下一入口；expected 不得悄悄弱化。
6. 提交前核对 code+tests+plan 同目标、所有 staged paths 为本轮所有；不为凑提交数拆空接口/占位或纯 progress。仅计划准备不冒充实现 checkpoint。
7. 有方向偏离先停止报告；不能用新 checkpoint 掩盖。没有完成闭环不标 verified。
8. 若启动了运行实例，按 ownership 确认收尾，不停止其他人的实例；确认无本轮遗留进程。
