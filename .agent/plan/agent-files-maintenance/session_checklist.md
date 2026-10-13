# Session Checklist

## 开工

1. 读 `./.agent/plan/agent-files-maintenance/plan.md` 的边界，确认本轮属哪个批次。
2. `git status` + `git rev-parse --short HEAD`：本仓库有并行 agent 活动，
   **HEAD 会移动**。提交一律用显式 pathspec，不要 `git add -A`。
3. 量而不猜：改动前先跑
   `python3 Script/ya_agent_docs_lint.py`（A2 之后）取基线。
4. 确认没碰 `.agent/plan/` 下别的活跃线（各自的 owner 在维护）。

## 收尾

1. 跑 `python3 Script/ya_agent_docs_lint.py`，退出码必须为 0。
2. 索引双向核对：`ls .agent/skills/*/` 与 `skills/AGENTS.md` 列表集合相等。
3. 链接可解析：相对链接按**各自文件所在目录**解析，不是按仓库根。
4. 提交前 `git diff --cached --stat` 核对——并行 agent 可能往 index 里塞了他们的改动。
5. plan 工件与本轮改动同一个提交。
6. 报出与本轮无关的既有改动，不要算进本轮。

## 危险动作

- **不要 `git add -A` / `git add .`**：工作区有用户与并行 agent 的未提交改动
  （`.agent/plan/AGENTS.md`、`.agent/plan/render-view-family/todo.md` 等）。
- **不要动 `plan/archive/`**：它的路径腐坏是设计如此（`plan/AGENTS.md` 已声明）。
- **不要把"符号失效"当结论直接清理**：先按词边界核验，多数是误报
  （MSVC 错误码 / 修饰名 / POSIX 宏），见 `progress.md`。
- **拆分 skill 必须与引用改写同批提交**：否则产生新的悬空链接。
- 不要改 `skills/AGENTS.md` 的**文件名**（外部可能按路径引用），只改内容结构。
