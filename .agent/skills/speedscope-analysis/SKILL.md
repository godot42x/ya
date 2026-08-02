---
name: ya-speedscope-analysis
description: 从 speedscope CPU trace 抽样提取文本视图，交给 AI 做性能分析。
---

## 适用场景

- 已产出 speedscope `.json`（`profile` 编译模式 / `--cpu-profile`），需要快速定位热点
- 想把大 trace（数十 MB 的事件流）转成精简文本，交给 AI 分析
- 需要对比不同帧的 CPU 耗时分布

## 核心工具

`Script/dump_speedscope.py`：解析 speedscope `evented` 事件流，重建调用树，按帧抽样输出文本。

```bash
python3 Script/dump_speedscope.py <trace.json> [-n N] [--start K] [--depth D] [--merge] [--long]
```

- `-n N`：抽样帧数（默认 3）
- `--start K`：跳过前 K 帧（场景加载 / 启动阶段）
- `--depth D`：限制树深度
- `--merge`：合并重复兄弟调用（强烈推荐）
- `--long`：保留完整函数签名

## 推荐命令

给 AI 分析用（跳启动、抽 3 帧、聚合、限深）：

```bash
python3 Script/dump_speedscope.py Engine/Saved/Profile/profile-latest.speedscope.json -n 3 --start 10 --merge --depth 5
```

## 输出格式

每帧一棵缩进调用树，节点含 `self=` / `total=` 耗时（ms）与 self 占比：

```
## Frame 1/3  dur=341.255 ms
  ya::ResourceResolveSystem::onUpdate  self=0.106ms total=210.467ms (0% self)
    ResourceResolveSystem.cpp:560 (ResourceResolve/Materials)  self=209.860ms total=209.860ms (100% self)
```

- `self` 为该函数自身（不含子调用）耗时
- `total` 为含子树总耗时
- 先看 `total` 大且 `self` 占比接近 100% 的叶子节点，即自耗热点

## 分析套路

1. 先跑默认命令拿概览，确认是 Logic 还是 Render 占大头。
2. 热点叶子（`self` 大）直接对应具体代码行（帧名带 `file:line`）。
3. 需要跨帧稳定性时，多抽几帧对比 `total` 是否稳定。

## 相关 skills

- `ya-profiling`：trace 的产出与配置入口
- `ya-debug-review`：耗时异常、回归复盘

## 退出条件

- 已拿到按帧的文本调用树
- 已能指认热点所在的 `file:line`
