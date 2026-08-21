# RenderGraph Import 复用失效：比较包装指针 vs 底层身份

适用场景：

- 日志每帧重复打印 `RenderGraph registry replacing texture 'XXX'`
- 同一批 imported 纹理（如 shadow depth、environment cubemap）每帧都被替换并重建
- `DeferredDeletionQueue::flush` 每帧都执行固定数量的析构（而不是只在启动/重建时执行）
- 明显是“资源复用失效”导致的性能退化

## 2026-08-22 回归结论

根因在 `isSameImportedTextureDesc`（`RenderGraphResourceRegistry.cpp`）。commit
`0f569de7 [render-graph] converge imports/exports to ImageResource owner` 把
import 复用判断从“底层 image/view 指针相同”错误地改成了“包装 `ImageResource`
指针相同”：

```cpp
// 错误：比较包装指针
lhs.resource.get() == rhs.resource.get()
```

但每次 `importTexture(makeImportedTextureDesc(resource, view, ...))` 都会走
`cloneImageResourceWithView`，**每帧 clone 一个新的 `ImageResource` 包装**（其
`image` / `defaultView` 指向稳定的底层 `IImage` / `IImageView`）。因此包装指针
每帧都变，复用判断永远 false，所有 imported 纹理每帧被替换 + 析构。

正确语义是比较**稳定的底层身份**：

```cpp
lhs.resource->getImageShared()     == rhs.resource->getImageShared()
lhs.resource->getImageViewShared() == rhs.resource->getImageViewShared()
```

## 关键约定

1. **Imported 资源的跨帧身份 = 底层 `IImage` / `IImageView` 指针，不是 `ImageResource` 包装指针。**
   `ImageResource` 只是一个“owner 聚合器 + retainedResources 容器”，可以每帧
   重建；只有底层 image/view 才是稳定身份。
2. `compileImportedTexture` 会把 `importDesc.nativeHandle` 强制 pin 到
   `image.getHandle()`，所以 `nativeHandle` 也是稳定的身份来源之一。
3. 任何“clone 一个包装、复用底层资源”的模式，判断复用时都只能比较底层身份，
   不能比较包装的 `shared_ptr` / 裸指针。

## 排查顺序

1. 先确认是不是 imported 资源（`ERGResourceLifetime::Imported`）每帧被替换。
2. 看 `needsTextureReplacement` / `isSameImportedTextureDesc` / `isSameImportedBufferDesc`
   里的身份比较，是否误用了每帧重建的包装指针。
3. 判断该 import 的底层 `IImage` / `IImageView` 是否跨帧稳定（如持久 shadow 纹理、
   environment cubemap），还是真的每帧新建（如 swapchain image）。
4. 只有底层资源真的变化时才应该触发替换。
