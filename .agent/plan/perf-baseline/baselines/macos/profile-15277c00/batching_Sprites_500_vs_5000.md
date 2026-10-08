# Benchmark comparison

- before: `/tmp/pb6-baseline/Sprites_500`
- after: `/tmp/pb6-baseline/Sprites_5000`

## Run metadata

| field | before | after |
| --- | --- | --- |
| scene | Sprites_500.scene.json | Sprites_5000.scene.json |
| commit | 15277c00a8c2d6367db8f1d726f0c8a3865e726a | 15277c00a8c2d6367db8f1d726f0c8a3865e726a |
| dirty checkout | no | no |
| build mode | profile | profile |
| gpu | Apple M5 | Apple M5 |
| graphics api | Vulkan 1.3.334 | Vulkan 1.3.334 |
| resolution | 1920x1080 | 1920x1080 |
| present mode | immediate | immediate |
| sampled frames | 600 | 600 |

## Metrics (marked beyond ±3%)

| section | metric | statistic | before | after | delta | % |
| --- | --- | --- | --- | --- | --- | --- |
| frame | cpuMs | p50 | 8.305 | 8.306 | 0.001 | +0.02% |
| frame | cpuMs | p95 | 13.176 | 12.844 | -0.331 | -2.52% |
| frame | frameMs | p50 | 8.365 | 8.344 | -0.020 | -0.24% |
| frame | frameMs | p95 | 13.252 | 12.894 | -0.357 | -2.70% |
| frame | gpuMs | p50 | 1.943 | 2.233 | 0.290 | +14.94% **▲** |
| frame | gpuMs | p95 | 3.076 | 3.492 | 0.416 | +13.54% **▲** |
| cpu_stages | App/FileWatcher | p50 | 0.035 | 0.028 | -0.007 | -20.72% **▼** |
| cpu_stages | App/FileWatcher | p95 | 0.050 | 0.038 | -0.012 | -24.06% **▼** |
| cpu_stages | Deferred/Light | p50 | 0.021 | 0.017 | -0.003 | -16.60% **▼** |
| cpu_stages | Deferred/Light | p95 | 0.041 | 0.025 | -0.016 | -38.79% **▼** |
| cpu_stages | Deferred/Light/Execute | p50 | 0.018 | 0.014 | -0.003 | -19.30% **▼** |
| cpu_stages | Deferred/Light/Execute | p95 | 0.036 | 0.022 | -0.014 | -38.65% **▼** |
| cpu_stages | Deferred/Light/Prepare | p50 | 0.000 | 0.000 | -0.000 | -50.60% **▼** |
| cpu_stages | Deferred/Light/Prepare | p95 | 0.000 | 0.000 | -0.000 | -83.20% **▼** |
| cpu_stages | Deferred/Overlay | p50 | 0.005 | 0.004 | -0.001 | -11.51% **▼** |
| cpu_stages | Deferred/Overlay | p95 | 0.007 | 0.005 | -0.002 | -30.40% **▼** |
| cpu_stages | Deferred/Shadow | p50 | 0.022 | 0.014 | -0.008 | -35.95% **▼** |
| cpu_stages | Deferred/Shadow | p95 | 0.060 | 0.029 | -0.031 | -51.62% **▼** |
| cpu_stages | Deferred/Tick | p50 | 0.804 | 0.751 | -0.054 | -6.67% **▼** |
| cpu_stages | Deferred/Tick | p95 | 1.289 | 0.901 | -0.388 | -30.09% **▼** |
| cpu_stages | Render/Begin | p50 | 0.021 | 0.015 | -0.006 | -30.04% **▼** |
| cpu_stages | Render/Begin | p95 | 0.044 | 0.022 | -0.023 | -51.31% **▼** |
| cpu_stages | Render/Extract | p50 | 0.429 | 0.400 | -0.029 | -6.67% **▼** |
| cpu_stages | Render/Extract | p95 | 0.573 | 0.451 | -0.122 | -21.33% **▼** |
| cpu_stages | Render/PrepareFrame | p50 | 0.024 | 0.015 | -0.009 | -38.46% **▼** |
| cpu_stages | Render/PrepareFrame | p95 | 0.049 | 0.023 | -0.027 | -54.19% **▼** |
| cpu_stages | Render/Presentation | p50 | 0.029 | 0.025 | -0.004 | -13.57% **▼** |
| cpu_stages | Render/Presentation | p95 | 0.053 | 0.037 | -0.016 | -29.86% **▼** |
| cpu_stages | Render/Runtime | p50 | 0.872 | 0.812 | -0.060 | -6.92% **▼** |
| cpu_stages | Render/Runtime | p95 | 1.438 | 0.991 | -0.447 | -31.09% **▼** |
| cpu_stages | Render/Submit | p50 | 3.874 | 3.664 | -0.210 | -5.43% **▼** |
| cpu_stages | Render/Submit | p95 | 9.796 | 9.079 | -0.717 | -7.32% **▼** |
| cpu_stages | Render/World | p50 | 0.807 | 0.753 | -0.054 | -6.67% **▼** |
| cpu_stages | Render/World | p95 | 1.301 | 0.907 | -0.394 | -30.29% **▼** |
| cpu_stages | Tick/EventPump | p50 | 0.045 | 0.032 | -0.012 | -27.46% **▼** |
| cpu_stages | Tick/EventPump | p95 | 0.075 | 0.054 | -0.021 | -28.33% **▼** |
| cpu_stages | Tick/FpsControl | p50 | 0.000 | 0.000 | -0.000 | -2.38% |
| cpu_stages | Tick/FpsControl | p95 | 0.000 | 0.000 | -0.000 | -74.70% **▼** |
| cpu_stages | Tick/Logic | p50 | 0.062 | 0.108 | 0.046 | +75.04% **▲** |
| cpu_stages | Tick/Logic | p95 | 0.102 | 0.202 | 0.100 | +98.47% **▲** |
| cpu_stages | Tick/MainThreadCallbacks | p50 | 0.001 | 0.000 | -0.000 | -78.56% **▼** |
| cpu_stages | Tick/MainThreadCallbacks | p95 | 0.001 | 0.000 | -0.001 | -78.09% **▼** |
| cpu_stages | Tick/Render | p50 | 8.245 | 8.192 | -0.053 | -0.64% |
| cpu_stages | Tick/Render | p95 | 13.109 | 12.713 | -0.396 | -3.02% **▼** |
| cpu_stages | Tick/RenderCallbacks | p50 | 0.000 | 0.000 | -0.000 | -87.41% **▼** |
| cpu_stages | Tick/RenderCallbacks | p95 | 0.001 | 0.000 | -0.001 | -81.03% **▼** |
| cpu_stages | Tick/Total | p50 | 8.305 | 8.306 | 0.001 | +0.02% |
| cpu_stages | Tick/Total | p95 | 13.176 | 12.844 | -0.331 | -2.52% |
| cpu_stages | Tick/Unaccounted | p50 | 0 | 0 | 0 | +0.00% |
| cpu_stages | Tick/Unaccounted | p95 | 0 | 0 | 0 | +0.00% |
| cpu_stages | Vulkan/Acquire | p50 | 0.014 | 0.010 | -0.004 | -26.82% **▼** |
| cpu_stages | Vulkan/Acquire | p95 | 0.030 | 0.014 | -0.016 | -52.38% **▼** |
| cpu_stages | Vulkan/Present | p50 | 0.020 | 0.013 | -0.007 | -33.83% **▼** |
| cpu_stages | Vulkan/Present | p95 | 0.039 | 0.021 | -0.018 | -45.34% **▼** |
| cpu_stages | iterate | p50 | 8.306 | 8.307 | 0.001 | +0.01% |
| cpu_stages | iterate | p95 | 13.177 | 12.845 | -0.332 | -2.52% |
| gpu_passes | Deferred EntityId.view4294967297 | p50 | 0.017 | 0.017 | 0.000 | +2.02% |
| gpu_passes | Deferred EntityId.view4294967297 | p95 | 0.017 | 0.018 | 0.001 | +3.59% **▲** |
| gpu_passes | Deferred Forward Opaque.view4294967297 | p50 | 0.009 | 0.009 | 0.001 | +7.25% **▲** |
| gpu_passes | Deferred Forward Opaque.view4294967297 | p95 | 0.010 | 0.010 | 0.001 | +5.63% **▲** |
| gpu_passes | Deferred Forward Transparent.view4294967297 | p50 | 0.007 | 0.007 | 0.000 | +2.16% |
| gpu_passes | Deferred Forward Transparent.view4294967297 | p95 | 0.008 | 0.009 | 0.000 | +1.51% |
| gpu_passes | Deferred GBuffer.view4294967297 | p50 | 0.038 | 0.033 | -0.006 | -14.95% **▼** |
| gpu_passes | Deferred GBuffer.view4294967297 | p95 | 0.580 | 0.617 | 0.038 | +6.49% **▲** |
| gpu_passes | Deferred Light.view4294967297 | p50 | 0.244 | 0.251 | 0.008 | +3.16% **▲** |
| gpu_passes | Deferred Light.view4294967297 | p95 | 0.263 | 0.269 | 0.006 | +2.12% |
| gpu_passes | Deferred Skybox.view4294967297 | p50 | 0.008 | 0.009 | 0.001 | +7.65% **▲** |
| gpu_passes | Deferred Skybox.view4294967297 | p95 | 0.009 | 0.013 | 0.004 | +40.04% **▲** |
| gpu_passes | Deferred Sprites.view4294967297 | p50 | 0.134 | 0.271 | 0.138 | +103.04% **▲** |
| gpu_passes | Deferred Sprites.view4294967297 | p95 | 0.142 | 0.365 | 0.222 | +156.15% **▲** |
| gpu_passes | Postprocessing.view4294967297 | p50 | 0.420 | 0.420 | -0.000 | -0.09% |
| gpu_passes | Postprocessing.view4294967297 | p95 | 1.108 | 0.642 | -0.465 | -42.02% **▼** |
| gpu_passes | Presentation | p50 | 0.307 | 0.011 | -0.296 | -96.42% **▼** |
| gpu_passes | Presentation | p95 | 1.194 | 1.228 | 0.034 | +2.81% |
| gpu_passes | SSAO Pass.view4294967297 | p50 | 0.267 | 0.286 | 0.020 | +7.39% **▲** |
| gpu_passes | SSAO Pass.view4294967297 | p95 | 0.331 | 0.449 | 0.119 | +35.87% **▲** |
| render_counters | descriptorBindCount | p50 | 7.000 | 7.000 | 0 | +0.00% |
| render_counters | descriptorBindCount | p95 | 7.000 | 7.000 | 0 | +0.00% |
| render_counters | drawCount | p50 | 3.000 | 3.000 | 0 | +0.00% |
| render_counters | drawCount | p95 | 3.000 | 3.000 | 0 | +0.00% |
| render_counters | drawIndexedCount | p50 | 2.000 | 2.000 | 0 | +0.00% |
| render_counters | drawIndexedCount | p95 | 2.000 | 2.000 | 0 | +0.00% |
| render_counters | pipelineBindCount | p50 | 7.000 | 7.000 | 0 | +0.00% |
| render_counters | pipelineBindCount | p95 | 7.000 | 7.000 | 0 | +0.00% |
| gpu_memory | bufferMiB | p50 | 0.033 | 0.033 | 0 | +0.00% |
| gpu_memory | bufferMiB | p95 | 0.033 | 0.033 | 0 | +0.00% |
| gpu_memory | renderTargetMiB | p50 | 256.4 | 256.4 | 0 | +0.00% |
| gpu_memory | renderTargetMiB | p95 | 256.4 | 256.4 | 0 | +0.00% |
| gpu_memory | textureMiB | p50 | 9.023 | 9.023 | 0 | +0.00% |
| gpu_memory | textureMiB | p95 | 9.023 | 9.023 | 0 | +0.00% |
| gpu_memory | totalMiB | p50 | 273.0 | 273.0 | 0 | +0.00% |
| gpu_memory | totalMiB | p95 | 273.0 | 273.0 | 0 | +0.00% |
| gpu_memory | transientMiB | p50 | 0 | 0 | 0 | +0.00% |
| gpu_memory | transientMiB | p95 | 0 | 0 | 0 | +0.00% |
| gpu_memory | uploadMiB | p50 | 7.542 | 7.542 | 0 | +0.00% |
| gpu_memory | uploadMiB | p95 | 7.542 | 7.542 | 0 | +0.00% |
| process_memory | peakRssMiB | p50 | 290.9 | 314.5 | 23.531 | +8.09% **▲** |
| process_memory | peakRssMiB | p95 | 290.9 | 314.5 | 23.531 | +8.09% **▲** |
| process_memory | rssMiB | p50 | 236.1 | 221.3 | -14.766 | -6.25% **▼** |
| process_memory | rssMiB | p95 | 256.3 | 307.8 | 51.423 | +20.06% **▲** |

## Facts

- `frame.fps`: 120.02601162915182 -> 119.9946252487437
- `gpu_memory.heaps`: [{"allocationMiB":272.95032501220703,"blockMiB":320.3515625,"budgetMiB":13107.199999809265,"heapIndex":0,"usageMiB":320.3515625}] (unchanged)
- `gpu_memory.peak.bufferMiB`: 0.03345489501953125 (unchanged)
- `gpu_memory.peak.renderTargetMiB`: 256.3515625 (unchanged)
- `gpu_memory.peak.textureMiB`: 9.0230712890625 (unchanged)
- `gpu_memory.peak.transientMiB`: 0.0 (unchanged)
- `gpu_memory.peak.uploadMiB`: 7.542236328125 (unchanged)
- `gpu_memory.supported`: True (unchanged)
- `process_memory.supported`: True (unchanged)
- `process_memory.unifiedMemory`: True (unchanged)

