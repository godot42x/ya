# TODO

## 本线剩余

- [ ] B2 脚本对外接口收口（排在 game-ui S7 之后；评审建议重新评估顺序）

## 评审待办（2026-09-30，未排期，细节见 `review-2026-09-30.md`）

性能 / 正确性
- [x] 双面 quad：精灵 / 延迟光照全屏 pass / billboard 都 `cullMode None` 画两遍 → 改剔除背面（见 progress 2026-09-30）
- [x] `Render/Frame` self 2.9–5ms 归因：主体是帧栅栏等待（`kFramesInFlight = 1`，CPU/GPU 串行，归 render-view-family M4），见 `r4-measurements.md` §7
- [ ] 精灵实例化（逐候选 ~1.2µs、四次命令调用）
- [ ] 纹理表每帧重建 + `slotFor` 线性查找 → 直接映射
- [ ] tilemap 每图层 / 区块静态实例缓冲，只在编辑时重建

2D 架构
- [ ] 2D 模块（`Scene2D` / `World2D`）：Sprite2D / Tilemap / Tileset 移出 `ya-render-3d`，拆开 `TilemapComponent.cpp` 的数据 / 查询 / 编辑 / 渲染展开
- [ ] 共享 2D quad 合批核心（世界精灵与 `ScreenDrawList` 两个前端）
- [ ] 纯 2D View 渲染家族（sprite + compose，画家算法 + y-sort，不写深度），之后删 `z = base − y·ε`
- [ ] 2D 相关计划线合并（rpg-prototype / scene-2d-world-and-game-ui / game-ui-script-framework / ui-behavior-capabilities）

脚本与硬编码
- [ ] 合并 `callNamed` 与 `invoke`
- [ ] `SpriteAnimation` 组件（D-T2 决策门已触发）
- [ ] `Sprite2DComponent` pivot / anchor
- [ ] 相机 2D 字段（每单位像素数、像素吸附、整数缩放）+ 相机跟随组件
- [ ] InputMap（命名动作）
- [ ] GUI 文本 reveal（逐字显示）
- [ ] 按格移动做成引擎 Lua 库
- [ ] 去硬编码：`"Player"`、`kSpriteTexture`、`viewAspect` 与 `viewSize` 重复、`TilemapGround` / `"Dialogue"` 命名约定、脚本函数里的 `App::get()`

验证
- [ ] 按键注入（`input.inject_key`）提前，补 R0–R3 行为的自动化

## 已拆出

- 资源层每帧 resolve → `../resource-handle-events/`
