# Session Checklist

## 开工前

- [ ] 阅读根 `AGENTS.md`、`.agent/plan/AGENTS.md`。
- [ ] 读 `ya-build`、`render-arch`；改到 GUI compose 时再读 `gui-framework`。
- [ ] `git status`，确认没有别的写者正在改同一批文件（本仓同时有并发写者）。
- [ ] 复述本轮唯一目标、边界、保留项、非目标。
- [ ] 用 `grep`/`rg` 核对真实符号与调用方，不用旧计划里的路径代替源码事实。
- [ ] 确认本轮不会把「排布」搬进 Framework/Render（见 plan §1）。

## 实施中

- [ ] 只改当前 checkpoint 需要的抽象、调用点与测试。
- [ ] 目录搬迁单独成刀：先 `git mv` 不改内容，再改 include；目标路径此前不存在，
      git 会按相似度识别为 rename，blame 保留。
- [ ] 不在 command recording 中途重建 GPU 资源；检查 deferred deletion / keepalive。
- [ ] 新增 include 显式写公开路径，不依赖别人传递进来的头。
- [ ] 不把 Forward / Deferred 抽成万能基类；不新增总入口式 `Renderer` 编排。

## 收尾前

- [ ] 受影响目标 build：`ya-game-runtime` / `ya-runtime` / `ya-game-editor` / `ya-testing`。
- [ ] `xmake r ya-render-3d-test`（期望 177/177）。
- [ ] `ya-testing` 滤镜跑一遍，与已登记基线比对（排除 `WidgetTreeTest.SystemLayersCannotBeDetached`）。
- [ ] `run_display_compose_parity.py --skip-build` 期望 PASS、md5 `c775245a…`。
- [ ] `run_widgettree_editor_smoke.py --skip-build` 期望 exit=0。
- [ ] `git diff --check`；确认没有动生成文件、`.vscode/settings.json`、根 `xmake.lua`、`Engine/Plugins/log.cc`。
- [ ] 更新 `progress.md` / `feature_matrix.json`。
- [ ] 明确记录保留项、未完成项、偏离项。
- [ ] 代码、测试、plan 文件同一 commit 提交，格式 `[render/app] …`。

## 最近一次 checkpoint

- 2026-09-22 AB1+AB2：`SceneRenderScheduler` 从 `AppRenderState` 的字段变成 `tickRender`
  的局部对象（`declareViews` 收 `SceneRenderScheduler&`，删 `SceneSchedulerGuard`）；
  `GameRuntime/Lifecycle/` 的四个渲染排布文件搬到 `GameRuntime/Render/`（纯 `git mv` +
  include 改写）。验证：四个目标 build ok；`ya-render-3d-test` 177/177；滤镜 681 passed /
  11 skipped / 6 failed（与基线同 6 个）；parity PASS（md5 `c775245a…`）；编辑器 smoke exit=0。
  下一刀：AB4（presentation 拆纯 pass + 应用侧 present，先解 primary-surface 耦合）。
