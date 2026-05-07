# Memory

## Animation Debugging

- 2026-05-01: skeletal animation once failed mainly because Slang shader macro parsing did not handle space-separated defines such as `"ENABLE_SKINNING 1"`; GLSL path already handled it, Slang path did not. Fix was in `Engine/Source/Render/Shader/SlangProcessor.cpp` to parse both `NAME=VALUE` and `NAME VALUE`.
- 2026-05-01: Slang does not accept `vector<mat4, MAX_BONE_COUNT>` for skinning palette storage. Use plain array syntax `mat4 boneMatrices[MAX_BONE_COUNT];` in `Engine/Shader/Slang/Common/Skinning.slang` for SSBO-compatible palette layout.
- 2026-05-01: `Engine/Source/Render/EngineGeometryNormalizer.cpp` had a skinning weight initialization bug. `SkeletonMeshVertex` tail influences were not cleared because the fill loop used `weightIdx < weightCount`. This could leave dirty bone ids / weights and produce unstable deformation. Always initialize `boneIDs = -1`, `weights = 0`, then fill valid influences.
- 2026-05-01: symptom evolution was `mesh static -> crash -> shadow animates in place while visible mesh disappears`. This strongly suggests the animation pose path is now producing non-trivial matrices, but one or more render passes are consuming a different transform convention.
- 2026-05-01: when `shadow moves but main mesh disappears`, first inspect root-transform handling and pass parity before suspecting ECS update order. Typical mismatch: shadow path samples one transform space while forward/deferred color passes sample another, or skinning matrix includes / misses model-root inverse.
- 2026-05-01: current likely hotspot is skeleton root transform compensation. Imported node hierarchy currently stores `scene->mRootNode->mTransformation`, but runtime skinning originally used only `global * offset`. We started testing `rootInverseTransform * global * offset`; this is the right axis to verify next.
- 2026-05-01: for the current `dancing_vampire.dae` path, blindly injecting `rootInverseTransform` into runtime skinning was not a safe fix. It changed visible-pass behavior without a matching importer/normalization story. Keep the simpler `global * offset` path until root-space normalization is solved end-to-end.
- 2026-05-01: shadow pass originally bound skinned meshes with `drawSkinned()` but its shader only read position from slot 0. That can explain “shadow frozen at origin / wrong pose” regressions. Shadow pass needs an actual skinning-aware vertex shader path if it renders skinned buckets.
- 2026-05-01: shadow pass should mirror the main render architecture: use separate static/skinned shader-pipeline variants, and push the real `skinningPaletteIndex` for skinned draws. A single always-skinned shadow pipeline is wasteful for static meshes and easy to wire incorrectly.
- 2026-05-01: for this kind of issue, prefer writing the concrete failure cause into `Memory.md` instead of promoting it to a skill. This is project-history / pitfall memory, not reusable architecture knowledge.

## Documentation Boundary

- Put stable cross-task abstractions, workflows, and subsystem maps into `./.agent/skills/*/SKILL.md`.
- Put debugging outcomes, repo-specific pitfalls, one-off regressions, local conventions that emerged during implementation, and “this bit us before” notes into `Memory.md`.
- If a note answers “what happened here before?” it belongs in memory; if it answers “how this subsystem is designed and should generally be worked with” it belongs in skills.
