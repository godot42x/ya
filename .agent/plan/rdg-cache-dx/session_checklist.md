# 每轮开工 / 收尾

开工：

- 读本目录 plan.md（目标与边界）、progress.md（上轮剩余问题）。
- 只进入当前 phase 对应的一个主 skill；不预读全部 skills / memories。
- 复述当前目标和边界，再实施。

收尾：

- `git diff --stat` 核对只含本目标改动；临时诊断代码已清理。
- `xmake b ya-render-3d-test && xmake r ya-render-3d-test` 全过。
- HelloMaterial 200 帧 flush 基线（除 warmup 外零 flush）不变。
- progress.md 记录完成内容、验证结果、剩余问题；plan 文件改动与代码同 commit。
- 提交格式 `[module] message`，精炼对应代码。
