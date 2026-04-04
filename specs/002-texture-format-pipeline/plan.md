# Implementation Plan: Texture Source Format Pipeline

**Branch**: `002-texture-format-pipeline` | **Date**: 2026-04-04 | **Input**: User request to audit and redesign texture import/upload format handling, especially `.hdr` and meta-driven overrides.

## Summary

The current texture pipeline accepts `.hdr` files at the asset-discovery and editor-selection level, but the actual decode path forces all image sources through 8-bit `stbi_load(..., STBI_rgb_alpha)` and stores them as `R8G8B8A8_(S)RGB`. That means `.hdr` is not actually preserved as HDR data. The plan is to refactor the pipeline so texture loading is split into three explicit stages: source inspection, decode-to-canonical CPU payload, and GPU upload selection. The chosen GPU format must be derived from detected source characteristics plus `.ya-meta.json` overrides, rather than hardcoded from color-space alone.

## Current Audit

### What exists today

- `AssetManager` recognizes `.hdr` as a texture asset type in meta inference and file selection.
- `AssetManager::decodeTextureToMemory()` always outputs `std::vector<uint8_t>` and `R8G8B8A8_(S)RGB` regardless of actual source bit depth.
- `AssetManager::loadTextureSync()` ultimately calls `Texture::fromFile(filepath, ..., bSRGB)`.
- `Texture::fromFile()` always uses `stbi_load(..., STBI_rgb_alpha)`, which converts to 8-bit RGBA before upload.
- `Texture::fromMemory()` can upload arbitrary `EFormat`, but the memory carrier in `AssetManager` is currently byte-only and therefore biased toward 8-bit textures.
- `AssetMeta` already supports free-form per-asset properties and is part of the texture cache key, so it is the correct control plane for format overrides.

### Current failure mode for `.hdr`

- `.hdr` files do not retain high dynamic range values.
- They are quantized to 8-bit RGBA on load.
- Any values above LDR range are lost before upload.
- Resulting texture format becomes `R8G8B8A8_SRGB` or `R8G8B8A8_UNORM`, which is semantically wrong for HDR content.

## Question 1: Impact Of Importing With Wrong Format

### Severity

High for lighting / skybox / IBL / emissive-reference content. Medium for general authoring UX. Usually not a total “cannot display” failure, but it can absolutely become a visually wrong or numerically invalid resource.

### Concrete impact categories

- Dynamic-range loss: HDR values are clamped/quantized into 8-bit, so bright highlights, exposure information, and lighting energy are lost permanently.
- Wrong sampling space: if linear HDR content is uploaded as sRGB, shader sampling applies gamma decode that should not exist, producing incorrect colors.
- Precision artifacts: normal/lookup/data textures imported with the wrong normalized format can exhibit banding, thresholds, unstable post effects, or broken material logic.
- Semantic mismatch: a texture intended as scene-linear radiance data cannot be used correctly for environment lighting if stored as 8-bit color.
- Cache poisoning: because cache keys currently do not model “detected source format vs overridden upload format” explicitly, a bad interpretation can persist until meta or asset invalidation happens.

### Will it ever fail to display entirely?

Yes, but less often than “displays incorrectly”. Cases include:

- Shader expectations mismatch: if a shader assumes float HDR range but gets 8-bit normalized data, output can appear nearly black, blown out, or useless for lighting.
- Unsupported future override: if we later allow explicit format overrides to a GPU format unsupported by a target backend/device and do not validate it, creation/upload can fail.
- Data-texture misuse: masks/LUTs/packed channels interpreted through the wrong format or color space can make downstream rendering effectively unusable even though sampling still technically works.

### Practical conclusion

Wrong format import is a correctness bug first, and a display failure second. For `.hdr`, the current behavior is already unacceptable for any physically meaningful workflow.

## Question 2: Meta Integration And Override Strategy

Yes. Import and upload must be explicitly combined with `.ya-meta.json`.

### Why meta should own overrides

- Detection is necessary but not sufficient. File extension and decoder inspection only tell us what the source is, not always how the asset should be used.
- The same underlying pixel data may be consumed as display color, lookup data, mask data, or lighting data.
- Overrides belong in the sidecar because they affect cache identity, hot reload, and editor-driven authoring.

### Required override categories

- `colorSpace`: `srgb` / `linear`
- `sourceKind`: `auto` / `ldr` / `hdr` / `data` / `compressed`
- `preferredUploadFormat`: `auto` / explicit `EFormat` string such as `R16G16B16A16_SFLOAT`
- `decodePrecision`: `auto` / `u8` / `f16` / `f32`
- `channelPolicy`: `preserve` / `force_rgba`
- `generateMips`, `filter`, and future sampler/import settings remain as-is

### Override resolution rule

1. Detect source file properties from the decoder.
2. Read `.ya-meta.json`.
3. Apply explicit overrides.
4. Validate the final resolved import/upload plan.
5. Hash the resolved plan into the cache key.

## Target Architecture

### Stage 1: Source Inspection

Introduce a small immutable description for the source file before upload.

Suggested fields:

- file path
- container/codec kind: png/jpg/hdr/dds/ktx/...
- detected bit depth / float-vs-unorm
- detected channel count
- dimensions
- whether source is HDR-capable
- whether source is compressed / precompressed

This stage must not decide final GPU format yet.

### Stage 2: Decoded CPU Payload

Replace the current byte-only `TextureMemoryBlock` with a typed payload model.

Suggested shape:

- `width`, `height`, `channels`
- `detectedSourceKind`
- `resolvedColorSpace`
- `resolvedUploadFormat`
- `payloadType`: `U8`, `F16`, `F32`, `CompressedBytes`
- owned byte storage plus stride/size metadata

Important point: CPU payload type must be independent from GPU format enum.

### Stage 3: Upload Plan

Introduce a resolver that maps source inspection + meta overrides into a final GPU upload plan.

Examples:

- PNG albedo: `R8G8B8A8_SRGB`
- PNG normal/data: `R8G8B8A8_UNORM`
- HDR environment map: `R16G16B16A16_SFLOAT` by default
- High-precision scientific/data texture: optionally `R32G32B32A32_SFLOAT` if later added, otherwise validated fallback

This logic should not live inline inside `AssetManager::decodeTextureToMemory()`.

## Refactor Plan

### Phase 0: Audit And Guardrails

- Add explicit runtime warnings when `.hdr` goes through the legacy 8-bit path.
- Add editor/runtime logging that prints resolved import plan once per texture load.
- Keep viewport/render fixes separate from texture-format work.

### Phase 1: Data Model Split

- Replace `TextureMemoryBlock::bytes`-only model with typed texture payload storage.
- Add a `TextureSourceInfo` or equivalent source inspection struct.
- Add a `ResolvedTextureImportSettings` struct derived from meta + detection.

### Phase 2: Decoder Refactor

- Route LDR sources through `stbi_load`.
- Route HDR sources through `stbi_loadf`.
- Preserve float data for HDR instead of forcing 8-bit RGBA.
- Decide whether the first implementation uploads HDR as `R16G16B16A16_SFLOAT` always, with optional later support for other float formats.

### Phase 3: Upload Refactor

- Extend `Texture::fromFile()` and `Texture::fromMemory()` so they no longer assume one decode type.
- Move file decoding out of `Texture::fromFile()` into `AssetManager` or a dedicated importer so upload functions operate on already-resolved payloads.
- Ensure `getFormatPixelSize()` and upload code correctly account for float formats used by imported HDR textures.

### Phase 4: Meta Overrides

- Extend `AssetMeta::defaultForTexture()` with new optional import settings, still defaulting to minimal authored JSON.
- Add parser helpers for explicit upload-format override strings.
- Fold resolved import settings into `propertiesHash()` semantics by storing them in meta-driven properties or by hashing the resolved plan.

### Phase 5: Validation And Tooling

- Validate unsupported combinations early with hard errors or editor-facing diagnostics.
- Add an asset-inspector section showing detected source info, resolved upload format, and whether override is active.
- Add test assets for `.png` LDR, `.hdr` float, and linear-data textures.

## Extensibility Goals

This refactor should make it straightforward to support:

- `.hdr` via float decode
- `.dds` / `.ktx2` passthrough or compressed upload
- cubemap faces with mixed source validation
- BC/ASTC/ETC import pipelines later
- offline import cooking in the future without rewriting runtime semantics

## Design Rules

- Source format detection and GPU upload format selection must be different steps.
- `EFormat` must describe the GPU resource, not the source file container.
- Color space is not enough to choose upload format.
- Meta overrides must be explicit, validated, and included in cache identity.
- Texture decode should produce a typed CPU payload, not always `std::vector<uint8_t>`.

## Source Files To Change

- `Engine/Source/Resource/AssetManager.h`
- `Engine/Source/Resource/AssetManager.cpp`
- `Engine/Source/Resource/AssetMeta.h`
- `Engine/Source/Resource/AssetMeta.cpp`
- `Engine/Source/Render/Core/Texture.h`
- `Engine/Source/Render/Core/Texture.cpp`
- optionally `Engine/Source/Editor/AssetInspectorPanel.cpp` for diagnostics UI

## Risks

- Cache invalidation bugs if old and new keys coexist incorrectly.
- Hidden assumptions that all imported textures are 8-bit RGBA.
- Incomplete backend format support if future overrides target formats not currently exposed in `EFormat`.
- Cubemap and batch-memory flows must be updated together, or HDR support will become inconsistent across code paths.

## Acceptance Criteria

- `.hdr` assets are decoded through a float path and uploaded as a float GPU format by default.
- `.png/.jpg` LDR assets keep current behavior unless meta overrides say otherwise.
- The resolved upload format is visible in logs and ideally editor inspector.
- `.ya-meta.json` can override detected format decisions safely.
- Wrong or unsupported override combinations fail loudly and deterministically.
