# 反射写入绕开 setter：子节点 world matrix 陈旧

## 症状

给相机实体（宿主）换位置后，它自动生成的机身网格（companion 子实体）留在原地，
而同一实体的视锥线框（直接读 authored transform）跟着走——"mesh 位置和 gizmo 线框对不上"。

## 根因

`TransformComponent` 的 world matrix 失效只有两条路：

1. setter（`setPosition/setRotation/setScale`）→ `_onChildrenDirtyCallback` → 子 Node 标脏；
2. `TransformSystem::updateNodeTree`：节点自己 `needsUpdate` 时重算自己。

第二条**只重算自己**，不标脏子节点。于是任何"字段直写"的写者（Inspector 属性图、
undo/redo、场景反序列化、脚本走反射）都会让宿主 Node 变脏、子 Node 保持 clean：
子节点带着旧 world matrix 继续绘制。setter 路径掩盖了这个问题（它会顺手标脏子节点），
所以只在"换了写者"或"宿主是场景根"（根节点从不经过 `onParentChanged` 安装回调）时暴露。

## 修法（两处互补，缺一不可）

- `TransformSystem::updateNodeTree`：节点重算后，对每个 child 调 `onHierarchyDirty()`。
  这是唯一不会被写者忘记的地方——父变了，子必然陈旧。
- `TransformComponent::onPostSerialize()`：反射/反序列化写完之后 `notifyChildrenDirty()`。
  直写字段的路径走不到 setter，但仍要在写点标脏子节点。

## 回归测试

`LinkageFrameworkTest.CameraBodyFollowsItsHostAfterTheCompanionExists`：覆盖 setter 路径、
反射 `onPostSerialize` 路径、嵌套后代、以及场景根路径。任一半被去掉，测试都会失败——
这正是当初只修 setter 时就漏掉的那一半。
