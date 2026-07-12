# GPU Resource API Inventory

## Snapshot

Inventory date: 2026-07-13.

- `IBuffer::create()`: 40 call sites
- `getTextureFactory()` / `ITextureFactory`: 34 references
- `Texture::createRenderTexture()`: 10 call sites

The important issue is not the call count. Resource creation currently has four independent entry paths:

- buffer static factory with a backend switch
- image/image-view texture factory
- sampler static factory through `App::get()`
- render-target/framebuffer backend-owned attachment creation

## Migration Update

The first two implementation batches completed the following changes:

- Buffer, sampler, image, imported image and image-view creation now use `IRenderResourceFactory`.
- `IBuffer::create()`, `Sampler::create()`, `ITextureFactory` and `VulkanTextureFactory` were removed.
- Swapchain images enter the resource layer through `ImportedImageDesc`.
- Cubemap views use the regular image-view descriptor instead of a specialized factory method.
- The remaining high-level texture creation descriptors moved from `TextureFactory.h` to `TextureCreateInfo.h`.

The factory still returns the existing shared resource types. Unique ownership and non-owning image-view semantics remain separate migration work and must not be inferred as complete from the unified creation entry.

The first render-intermediate migration also established these boundaries:

- `RenderingInfo::ImageSpec` consumes non-owning `IImage`/`IImageView` references instead of an asset `Texture`.
- `RenderImage` is the temporary legacy owner for an image and its default view until the RenderGraph registry owns transient resources.
- BRDF LUT, Deferred SSAO and bloom intermediates now use `RenderImage`.
- The final postprocess output still crosses the pipeline/App/automation screenshot boundary as `Texture*`; migrate it together with screenshot capture ownership.
- Shadow sampled views and screenshot scratch resources still use `Texture` wrappers and remain migration work.

## Buffer Categories

Recommended migration order:

1. Staging and readback
   - texture uploads in `Render/Core/Texture.cpp`
   - automation screenshot readback
2. Per-frame UBO/SSBO
   - Deferred GBuffer/SSAO/overlay
   - Forward lit/unlit/aux passes
   - shadow directional/point/cull/indirect passes
3. Persistent geometry and infrastructure
   - mesh and Render2D vertex/index buffers
   - material descriptor pools
   - indirect buffers

The staging/readback group is the first useful migration sample because ownership and memory direction are explicit.

## Image And View Categories

`ITextureFactory` currently serves two distinct roles:

- create an owning image resource
- create a derived view over an existing image

Derived views are common and must remain first-class in the new API:

- editor color-channel preview views
- cubemap face and mip preview views
- environment preprocess face/mip views
- directional and point-shadow sampled views

A derived-view cache key, if caching is introduced, must include:

- image identity
- view type
- aspect
- base mip and mip count
- base layer and layer count
- component mapping

## Texture Roles To Separate

Current `Texture` instances represent four different concepts:

1. Asset texture
   - imported file/memory data
   - fallback/font/generated asset data
2. Render intermediate
   - SSAO
   - bloom and postprocess
   - BRDF LUT
   - screenshot scratch
3. Existing-image wrapper
   - framebuffer attachment
   - swapchain/external image
4. Subresource binding wrapper
   - cubemap face/mip
   - shadow face/layer

Only the first category remains `Texture` in the target model. Categories 2-4 become GPU image/view ownership or non-owning bindings.

## Current Ownership Chain

```text
IRenderTarget
  -> IFrameBuffer
     -> Texture wrapper
        -> shared IImage
        -> shared IImageView

VulkanRenderTarget
  -> owned VulkanImage, or imported swapchain VulkanImage
  -> VulkanFrameBuffer creates another view/wrapper layer
```

This ownership chain must not be copied into the RenderGraph registry. The target graph chain is:

```text
RenderGraphResourceRegistry
  -> owned/imported IGpuImage
  -> owned default/derived IGpuImageView

RGPassContext
  -> non-owning resolved references
```

## Imported Image Contract

The implementation must decide explicitly for every imported image:

- who owns and destroys the native image
- whether the registry owns derived views
- initial resource state
- required final resource state
- extent/format/usage supplied by the importer
- whether debug naming the native object is permitted

Swapchain images are non-owning imports. Shadow and environment resources are initially persistent external resources and may later move into graph ownership.

## Isolated Upload Boundary

Image creation is currently mixed with `beginIsolateCommands()` upload and initial layout transitions. Migration must separate:

- allocation: resource factory
- data decode: asset/import layer
- staging/copy/initial state: upload service

Moving allocation to the new factory without separating upload/state behavior would leave layout ownership ambiguous.

## RenderTargetPool Boundary

`RenderTargetPool` currently pools `IRenderTarget`. It must not become a second transient resource allocator beside `RenderGraphResourceRegistry`.

- Legacy paths may continue using it during migration.
- New graph passes must not allocate through it.
- Delete or narrow it after Deferred and Forward graph migration establishes the physical-resource reuse policy.
