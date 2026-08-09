# SceneManager::destroySceneIfNeeded 别名引用导致 onSceneDestroy 静默丢失

> 2026-08-09，修 Linkage 回调解绑时发现（既有 bug，影响所有 onSceneDestroy 监听）。

## 现象

- LinkageFramework/PhysicsSystem/App 的 `onSceneDestroy` 监听在 unload 时
  从未触发；规则不解除 entt 连接，随后在规则析构里访问已销毁的 registry
  → entt `fast_mod` 断言（dense_map bucket_count=0，即对象已析构/被 move）。
- 只有"销毁 active scene"路径受影响：`unloadScene() -> destroyScene(_activeScene)`
  传的是 `_activeScene` 的**引用**。

## 根因

```cpp
void SceneManager::destroySceneIfNeeded(stdptr<Scene>& scene)
{
    if (_activeScene == scene) {
        _activeScene.reset();            // scene 别名 _activeScene → 对象当场析构
    }
    onSceneDestroyInternal(scene.get()); // scene.get() 已是 nullptr → 提前 return
    scene.reset();
}
```

`scene` 是 `_activeScene` 的引用：`_activeScene.reset()` 把唯一强引用释放
（`_reg2scene` 只存裸指针，不持有引用计数）→ Scene 析构 → 广播拿到 nullptr
静默跳过。而 `destroyScene(局部 shared_ptr)`（测试/编辑器 play session 常见）
没有别名问题，广播正常——所以单测与编辑器路径掩盖了该 bug。

## 修复

先广播、后释放：

```cpp
onSceneDestroyInternal(scene.get());
if (_activeScene == scene) _activeScene.reset();
scene.reset();
```

## 预防

1. 生命周期回调必须在"最后一个强引用释放**之前**"发出；释放与通知不可交换
   顺序。
2. 以引用形式传入成员 shared_ptr 的函数，reset 前先想清楚别名语义。
3. 测试要覆盖"通过 manager 销毁 active scene"（`unloadScene()`），不能只测
   局部 shared_ptr 的 `destroyScene()`——两条路径行为可能不同。
