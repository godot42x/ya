# Session Checklist

## 开工

1. 读根 `AGENTS.md`，再读 `./.agent/skills/code-reorganize/SKILL.md` 的
   "头文件布局与 include 规则"。
2. 确认当前阶段与边界：S1 已落地，下一刀从 S2 清单里选**一条**。
3. `git status` 确认工作区；用户正在改的文件一律不碰，并在收尾时报出来。
4. 动手前先量：用 `rg` 统计真实消费面，不靠猜。

## 收尾

1. 迁移脚本先 dry-run 再 apply；不要把文件列表塞进 shell 变量
   （`FILES=$(...)` 在 zsh 下不会按空白分词，会变成单个文件名）。
2. 结构自检：0 转发 stub、0 多公开路径、0 未解析 include、0 失效
   `add_headerfiles` 模式。
3. `xmake b` 单 target 逐个跑（xmake 3.0.8 一次只接受一个 target）。
4. 大搬迁检查 blame：`git show -M --summary` 的 rename 数与 similarity。
5. 报出与本轮无关的既有失败，不要把别人的坑算进本轮。
6. plan 工件与本轮代码同一个提交。

## 允许的例外

- 纯搬迁可以拆成"先删镜像 / 再搬真身"两次提交，这是保留 blame 的必要形态；
  第一次提交不构建属于该形态的一部分，需在提交信息里说明。

## 危险动作

- 不要用 `git restore` / `git checkout --` 一次性恢复整个工作区：会把用户
  未提交的改动一起回滚。恢复前显式列出保护名单并逐个排除。

