# FIF 1→2 A/B（R1 验收，同构建同钩子口径）

同一 commit（100ae306）同一构建，仅 `kFramesInFlight` 1/2 之差；A/B 各 9 配置 × 3 次，
profile、trace off、immediate、uncapped。`Meshes_5000/10000` 双侧都超时（见 progress 的
分配钩子发现），无数字。

| 场景 | FIF=1 frame p50 | FIF=2 frame p50 | Δ |
| --- | --- | --- | --- |
| Lights_200 | 13.20 | 9.23 | **-30.1%** |
| Lights_50 | 11.54 | 8.61 | **-25.4%** |
| Lights_500 | 14.68 | 8.84 | **-39.8%** |
| Meshes_1000 | 10.98 | 8.30 | **-24.5%** |

与 `profile-15277c00` 不可比：那份基线的 commit 早于全局分配钩子（5fc2a91fa）落库，
钩子把 MoltenVK 每次 `vkCmd*` 的内部分配计入记账，重 draw 场景被放大 ~20×
（`Meshes_10000` 在 FIF=1+钩子下同样 900s 超时，与 FIF 无关）。自这份起，同构建 A/B
是前后对比的正确口径，直到分配钩子有了基准开关（perf-baseline 新待办）。
