# 共享分支上并行提交时，`--amend` 会打到别人的提交

适用场景：

- 两个 worker 在同一个工作区、同一条分支上同时 `git commit`
- 有人用 `git commit --amend` 补自己刚做的提交，结果改写的是另一个 worker 的 HEAD

## 事故（2026-10-12）

GUI 审查修正和 GA3r 的 worker 共用 `main` 的工作区。修正流程里一次 `git commit --amend` 落在操作间隙：HEAD 已经换成并行 worker 刚推上来的提交，amend 改写的是那一笔，不是自己的。

当时用 reflog 定位，再用 `read-tree` + `commit-tree` 把链重建回来，工作区文件没有被这次 amend 改掉。重建能挽回内容，但不能当作常规做法。

## 规则

- amend 之前先核对 `git rev-parse HEAD` 仍是自己刚形成的那笔提交。hash 变了就停，改成新提交，不要 amend。
- 同一时刻只允许一个 worker 在共享工作区提交。要并行提交，就串行排队，或者各自用独立 worktree。
- 父 agent 在 subagent 还在跑的时候不要 `git add` / `git commit`，暂存区是工作区共用的。
