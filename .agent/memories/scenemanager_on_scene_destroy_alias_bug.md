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

## 同族变体（2026-08-10，PIE Stop 崩溃）

**现象**：编辑器点 Stop（runtime stop）崩溃，栈：
`EditorPlaySession::end -> SceneManager::destroyScene -> onSceneDestroy 广播
-> LinkageFramework::onSceneDestroy -> LightBillboardLinkageRule::onSceneUnload
-> disconnectScene -> entt::registry::assure -> dense_map::find -> fast_mod
断言`（bucket_count 非 2 的幂 = 读取已释放对象）。

**根因**：`destroyScene(_playScene)` 传的是 EditorPlaySession::_playScene 的
**引用**，而播放场景已不是 active scene（end 先 activate 回 authoring），
`_playScene` 是唯一强引用。广播期间第一个监听者
`AppLifecycle::onSceneDestroy -> notifyModulesSceneDestroyed ->
EditorPlaySession::onSceneDestroyed -> _playScene.reset()` 把唯一引用释放
→ Scene 在广播中途析构 → 后续监听者（Linkage 规则断开 entt 信号）拿到
悬垂 Scene/registry。与 2026-08-09 变体是同一类：**广播期间有人释放
场景的最后一个强引用**。

**修复**：`destroySceneIfNeeded` 在广播期间持有 keep-alive 强引用：

```cpp
const stdptr<Scene> keepAlive = scene;   // 广播期间 Scene 保证存活
onSceneDestroyInternal(scene.get());
if (_activeScene == scene) _activeScene.reset();
scene.reset();                           // keepAlive 在函数尾释放
```

**护栏**：`SceneManagerLifecycleTest.DestroyBroadcastKeepsSceneAliveForLaterListeners`
（自定义 deleter 精确断言场景不在广播中途析构；去掉 keep-alive 必失败）。

**教训**：给 SceneManager 的 destroy/activate 广播加"对象存活契约"——广播
期间管理器必须持有强引用；任何监听者在回调里 reset 外部唯一引用都会触发
同类问题。

## 预防

1. 生命周期回调必须在"最后一个强引用释放**之前**"发出；释放与通知不可交换
   顺序。
2. 以引用形式传入成员 shared_ptr 的函数，reset 前先想清楚别名语义。
3. 测试要覆盖"通过 manager 销毁 active scene"（`unloadScene()`），不能只测
   局部 shared_ptr 的 `destroyScene()`——两条路径行为可能不同。
