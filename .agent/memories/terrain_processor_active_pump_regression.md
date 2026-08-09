# TerrainProcessor 拆分时丢失 active 重泵循环导致 terrain 不渲染

> 2026-08-09，f5670573 `[render] split terrain processing out of environment lighting`。

## 现象

- 场景中 TerrainComponent 不渲染；无任何 terrain 相关报错日志。
- 自动化截图永远不触发（`hasPendingTerrainResolve` 恒为 true），runtime 一直跑不到 stable。
- 环境光照（skybox/irradiance/prefilter）正常，只有 terrain 挂了。

## 根因

原 EnvironmentLightingProcessor::resolvePendingTerrain 的末尾（a5dd54c4 起）有：

```cpp
std::vector<entt::entity> activeEntities(_activeTerrain.begin(), _activeTerrain.end());
for (const auto entity : activeEntities) {
    pumpOne(entity);
}
```

抽成 TerrainProcessor 时这段被丢掉了。后果：terrain 首次 pump 进入
`LoadingHeightMap`（异步 batch 解码中）后永远不会被再次 pump——audit 会跳过
active 实体（`isTerrainQueuedOrActive`），dirty 队列又已空，
`consumeTextureBatchMemory` 永远等不到 → `getTerrainMesh()` 恒返回 nullptr →
RenderFrameExtractor 不发 terrain draw item。

## 为什么测试没抓到

- 350 单测不覆盖该状态机（无 terrain 测试）。
- 冒烟若用不含 terrain 的场景（或不做截图自动化），不触发
  `hasPendingTerrainResolve` 阻塞路径。

## 同类模式（确认保留）

- EnvironmentLightingProcessor：skybox（1302 行附近）、environment（2501 行附近）
  都有 active 重泵循环。
- GameplayResourceBinding：`_activeMaterial` 重泵循环保留。

## 预防

1. 拆分处理器时，逐函数核对"循环末尾的 active 重泵段"是否随搬移保留
   （`rg -n "activeEntities" <文件>` 应非空）。
2. 冒烟必须包含带 TerrainComponent 的场景并跑截图自动化
   （`hasPendingTerrainResolve` 是现成的回归探针：stable 判定即 terrain Ready）。
3. 与 `module_split_sed_regression.md` 同一失败类别：大块搬迁/删除后核对
   函数与循环完整性，不只看"能编译、单测过"。
