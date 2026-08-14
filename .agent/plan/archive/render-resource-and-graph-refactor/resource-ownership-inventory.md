# GPU Resource Ownership Inventory

## Snapshot

Inventory date: 2026-07-14.

This note records the current owner, downstream readers and destruction order for the main GPU resource shapes that remain in the repo after the startup-stability fixes.

It is not the target design. It is the current truth that the next refactor steps must preserve or replace deliberately.

## 1. Asset Texture Chain

Current shape:

```text
AssetManager / TextureLibrary / caller-owned shared_ptr<Texture>
  -> Texture
     -> shared_ptr<IImage>
     -> shared_ptr<IImageView> (default view)
```

Typical readers:

- materials and descriptor updates
- editor previews
- runtime sprite/UI systems
- screenshot/presentation fallbacks in legacy paths

Current destruction behavior:

- owners usually replace `shared_ptr<Texture>` and retire the old object through `DeferredDeletionQueue`
- destroying `Texture` releases its shared image and shared default view together

Observations:

- asset lifetime and GPU object lifetime are still bundled
- `Texture` remains the only high-level object that naturally spans asset metadata and GPU residency

## 2. RenderImage Chain

Current shape:

```text
stage/pipeline/shared-resource owner
  -> shared_ptr<RenderImage>
     -> shared_ptr<IImage>
     -> shared_ptr<IImageView> (default view)
```

Current owners:

- `RenderSharedResourceProvider` for BRDF LUT
- `RenderGraphResourceRegistry` for graph-owned logical textures
- postprocess / bloom / SSAO runtime state when materialized outside pure asset paths

Typical readers:

- pass execute callbacks through raw `RenderImage*`
- descriptor updates using non-owning `IImage*` / `IImageView*`
- debug views and screenshot capture

Current destruction behavior:

- replacement usually retires the old `shared_ptr<RenderImage>` through `DeferredDeletionQueue`
- in test/tool paths without an initialized deletion queue, destruction may happen immediately

Observations:

- `RenderImage` is the current “GPU intermediate owner” stopgap
- it already matches the intended boundary better than `Texture`, but still uses shared ownership rather than a strict owner/reference split

## 3. RenderTarget / FrameBuffer / Wrapped Texture Chain

Current shape:

```text
IRenderTarget
  -> frame buffer array
     -> Texture::wrap(...)
        -> shared_ptr<IImage>
        -> shared_ptr<IImageView>
```

Important variants:

- regular offscreen attachments created by the backend
- swapchain-backed presentation target importing native swapchain images
- shadow-face / layer adapters still using `Texture::wrap()` locally

Current destruction behavior:

- `VulkanRenderTarget::destroy()` clears framebuffer storage after `waitIdle()`
- `VulkanFrameBuffer::clean()` releases wrapped textures/views
- wrapped `Texture` destruction then releases the shared image/view references

Observations:

- this chain duplicates the “image + default view” ownership already represented by `RenderImage`
- swapchain-backed targets still force `Texture` wrappers into a path that should eventually become pure attachment/view references

## 4. RenderGraph Resource Registry Chain

Current shape:

```text
RenderGraphExecutor
  -> RenderGraphResourceRegistry
     -> owned texture entry (shared_ptr<RenderImage>)
     -> owned buffer entry (shared_ptr<IBuffer>)
```

Typical readers:

- `RGRenderContext::resolveTexture()` / `resolveBuffer()`
- runtime code that pulls persistent graph outputs back out after execute

Current destruction behavior:

- `sync()` replaces stale entries when desc/import contracts change
- replaced entries are retired through `DeferredDeletionQueue` when available
- `clear()` drops all registry-held references

Observations:

- registry already owns the most graph-like lifetime domain in the codebase
- persistent graph outputs still escape as raw pointers, so caller code must not outlive the registry entry that produced them

## 5. Imported Image Chain

Current shape:

```text
external/native image handle
  -> ImportedImageDesc
  -> IRenderResourceFactory::importImage()
  -> shared_ptr<IImage>
  -> optional owned default/derived view
```

Current imported sources:

- swapchain images
- existing `Texture` / `RenderImage` GPU images re-imported into RenderGraph

Ownership rule today:

- native image lifetime remains external
- imported `IImage` wrapper is owned by the importer / registry
- derived/default views created after import are owned by the importer / registry, not by the native source

Observations:

- this is the seam where the future “non-owning native resource + owning view cache” contract will need to become explicit
- swapchain import is the most sensitive case because image index selection and present-state requirements are frame-local

## 6. Screenshot / Offscreen Scratch Hotspots

Remaining legacy ownership hotspots:

- some shadow pass local face/layer bindings still use `Texture::wrap()`

Why they matter:

- they keep GPU intermediate ownership coupled to `Texture`
- they blur whether screenshot/offscreen jobs consume asset textures, graph outputs or transient render attachments

Current note:

- `AppScreenshotCapture` no longer allocates an `AutomationScreenshotScratch` render texture
- screenshot offscreen jobs now optionally run without an output `Texture`, copying directly from the chosen source image into the readback buffer

## 7. Current Destroy Order Rules

The effective destroy order today is:

1. stop producing GPU work or wait for a safe frame boundary
2. retire owning `shared_ptr<Texture>` / `shared_ptr<RenderImage>` / imported wrappers through `DeferredDeletionQueue` when GPU use may still be in flight
3. release image-view owners before image owners, usually implicitly because both are bundled inside the same outer owner
4. destroy backend containers (`VulkanFrameBuffer`, `VulkanRenderTarget`, registry entries) after they drop wrapped resource references

This is safe enough for the current shared-ownership model, but it hides the intended future rule:

- image view is non-owning
- image has a single durable owner
- pass/executor/editor only borrow references

## 8. Refactor Consequences

Immediate implications for the next plan items:

- migrating screenshot scratch resources off `Texture` will remove one of the last “GPU intermediate as asset object” paths
- replacing `Texture::wrap()` in swapchain/render-target paths will directly simplify the RenderTarget split
- once graph outputs no longer escape as `RenderImage*` across broad runtime surfaces, the registry can tighten ownership away from shared pointers
