# Session Checklist

## 开工

1. 读 `./.agent/plan/editor-ui-grouping/plan.md` 的边界与分组判据，再看
   `./.agent/skills/code-reorganize/SKILL.md` 的头文件布局规则。
2. `git status` 确认工作区；用户正在改的文件一律不碰，收尾时报出来。
3. 动手前先量：要搬的目录里到底有多少文件、多少 include 指向它
   （`rg -c`），不靠猜。
4. 只有能回答"这个文件属于哪个关切"的才搬；答不上来的留在原地并记一笔。

## 收尾

1. 结构自检：平铺残留 0、旧路径引用 0、`GameEditor/*` include 全解析。
2. `xmake b` 单 target 逐个跑（xmake 3.0.8 一次只接受一个 target）：
   `ya-game-editor`、`ya-testing`、`ya-game-runtime`、`ya-engine`。
3. **搬迁后必须重新构建**：重排 unity 批次会暴露平铺时被掩盖的重复私有符号。
   看到 duplicate symbol 不要加命名空间糊过去，抽私有头或改名。
4. 测试**集合对比** HEAD 基线，不要凭印象判断哪些失败是既有的：
   `git stash push -- <本轮改动的目录>` 探针 → 跑 → `git stash pop`。
5. 源码守卫测试会 `readEngineSource(...)` 后 grep 路径；搬目录后必须全仓
   `rg` 一次旧路径（排除 `build/` 与 `.agent/plan/`）。
6. 报出与本轮无关的既有失败，不要把别人的坑算进本轮。
7. plan 工件与本轮代码同一个提交。

## 危险动作

- 不要用 `git restore` / `git checkout --` 恢复整个工作区：会把用户未提交的
   `.vscode/settings.json`、`Engine/Plugins/log.cc`、`xmake.lua` 改动一起回滚。
  恢复前显式列出保护名单并逐个排除。
- 不要为"保留 blame"去改已经健康的搬迁形态：新路径搬迁天然是 rename，
   "先删镜像再搬真身"只在目标路径已存在同名镜像时才需要。
