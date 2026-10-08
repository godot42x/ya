# Benchmark comparison

- before: `/tmp/pb6-baseline/Lights_50/run1`
- after: `/tmp/pb6-baseline/Lights_50/run3`

## Run metadata

| field | before | after |
| --- | --- | --- |
| scene | Lights_50.scene.json | Lights_50.scene.json |
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
| frame | cpuMs | p50 | 8.344 | 8.336 | -0.008 | -0.10% |
| frame | cpuMs | p95 | 10.860 | 10.674 | -0.186 | -1.71% |
| frame | frameMs | p50 | 8.384 | 8.373 | -0.011 | -0.14% |
| frame | frameMs | p95 | 10.973 | 10.719 | -0.254 | -2.32% |
| frame | gpuMs | p50 | 4.722 | 4.783 | 0.061 | +1.29% |
| frame | gpuMs | p95 | 6.698 | 6.541 | -0.158 | -2.35% |
| cpu_stages | App/FileWatcher | p50 | 0.021 | 0.020 | -0.000 | -2.13% |
| cpu_stages | App/FileWatcher | p95 | 0.036 | 0.032 | -0.005 | -13.19% **▼** |
| cpu_stages | Deferred/Light | p50 | 0.014 | 0.014 | -0.000 | -0.30% |
| cpu_stages | Deferred/Light | p95 | 0.022 | 0.021 | -0.001 | -4.06% **▼** |
| cpu_stages | Deferred/Light/Execute | p50 | 0.011 | 0.011 | -0.000 | -0.74% |
| cpu_stages | Deferred/Light/Execute | p95 | 0.018 | 0.017 | -0.002 | -8.20% **▼** |
| cpu_stages | Deferred/Light/Prepare | p50 | 0.000 | 0.000 | 0 | +0.00% |
| cpu_stages | Deferred/Light/Prepare | p95 | 0.000 | 0.000 | -0.000 | -49.97% **▼** |
| cpu_stages | Deferred/Overlay | p50 | 0.003 | 0.003 | 0.000 | +5.07% **▲** |
| cpu_stages | Deferred/Overlay | p95 | 0.005 | 0.005 | -0.000 | -0.87% |
| cpu_stages | Deferred/Shadow | p50 | 0.017 | 0.016 | -0.001 | -3.13% **▼** |
| cpu_stages | Deferred/Shadow | p95 | 0.050 | 0.040 | -0.010 | -20.02% **▼** |
| cpu_stages | Deferred/Tick | p50 | 1.640 | 1.682 | 0.042 | +2.58% |
| cpu_stages | Deferred/Tick | p95 | 2.501 | 2.485 | -0.016 | -0.63% |
| cpu_stages | Render/Begin | p50 | 0.009 | 0.009 | -0.000 | -4.35% **▼** |
| cpu_stages | Render/Begin | p95 | 0.028 | 0.022 | -0.006 | -20.79% **▼** |
| cpu_stages | Render/Extract | p50 | 0.001 | 0.001 | 0.000 | +5.18% **▲** |
| cpu_stages | Render/Extract | p95 | 0.002 | 0.002 | -0.000 | -12.18% **▼** |
| cpu_stages | Render/PrepareFrame | p50 | 0.021 | 0.021 | -0.000 | -0.49% |
| cpu_stages | Render/PrepareFrame | p95 | 0.064 | 0.050 | -0.013 | -20.76% **▼** |
| cpu_stages | Render/Presentation | p50 | 0.022 | 0.021 | -0.001 | -3.45% **▼** |
| cpu_stages | Render/Presentation | p95 | 0.037 | 0.034 | -0.003 | -8.66% **▼** |
| cpu_stages | Render/Runtime | p50 | 1.713 | 1.738 | 0.024 | +1.41% |
| cpu_stages | Render/Runtime | p95 | 2.581 | 2.571 | -0.009 | -0.36% |
| cpu_stages | Render/Submit | p50 | 0.473 | 0.464 | -0.009 | -1.90% |
| cpu_stages | Render/Submit | p95 | 4.849 | 4.736 | -0.113 | -2.33% |
| cpu_stages | Render/World | p50 | 1.642 | 1.684 | 0.042 | +2.55% |
| cpu_stages | Render/World | p95 | 2.504 | 2.491 | -0.013 | -0.50% |
| cpu_stages | Shadow/Directional | p50 | 0.133 | 0.134 | 0.001 | +0.67% |
| cpu_stages | Shadow/Directional | p95 | 0.199 | 0.197 | -0.002 | -0.88% |
| cpu_stages | Shadow/Point | p50 | 0.719 | 0.767 | 0.048 | +6.74% **▲** |
| cpu_stages | Shadow/Point | p95 | 1.132 | 1.127 | -0.005 | -0.45% |
| cpu_stages | Shadow/Point/DirectDrawStatic | p50 | 0.114 | 0.122 | 0.008 | +6.96% **▲** |
| cpu_stages | Shadow/Point/DirectDrawStatic | p95 | 0.181 | 0.180 | -0.001 | -0.41% |
| cpu_stages | Shadow/Point/FaceDirect | p50 | 0.114 | 0.122 | 0.008 | +6.99% **▲** |
| cpu_stages | Shadow/Point/FaceDirect | p95 | 0.181 | 0.180 | -0.001 | -0.39% |
| cpu_stages | Shadow/Point/FaceLoop | p50 | 0.718 | 0.767 | 0.048 | +6.74% **▲** |
| cpu_stages | Shadow/Point/FaceLoop | p95 | 1.132 | 1.127 | -0.005 | -0.45% |
| cpu_stages | Shadow/Point/FaceSkinned | p50 | 0.000 | 0.000 | 0 | +0.00% |
| cpu_stages | Shadow/Point/FaceSkinned | p95 | 0.000 | 0.000 | 0 | +0.00% |
| cpu_stages | Tick/EventPump | p50 | 0.024 | 0.023 | -0.001 | -3.32% **▼** |
| cpu_stages | Tick/EventPump | p95 | 0.045 | 0.038 | -0.007 | -16.49% **▼** |
| cpu_stages | Tick/FpsControl | p50 | 0.000 | 0.000 | 0 | +0.00% |
| cpu_stages | Tick/FpsControl | p95 | 0.000 | 0.000 | -0.000 | -1.19% |
| cpu_stages | Tick/Logic | p50 | 0.027 | 0.026 | -0.001 | -2.57% |
| cpu_stages | Tick/Logic | p95 | 0.056 | 0.047 | -0.009 | -16.61% **▼** |
| cpu_stages | Tick/MainThreadCallbacks | p50 | 0.000 | 0.000 | 0 | +0.00% |
| cpu_stages | Tick/MainThreadCallbacks | p95 | 0.001 | 0.000 | -0.000 | -31.33% **▼** |
| cpu_stages | Tick/Render | p50 | 8.306 | 8.305 | -0.001 | -0.01% |
| cpu_stages | Tick/Render | p95 | 10.833 | 10.656 | -0.176 | -1.63% |
| cpu_stages | Tick/RenderCallbacks | p50 | 0.000 | 0.000 | 0 | +0.00% |
| cpu_stages | Tick/RenderCallbacks | p95 | 0.000 | 0.000 | -0.000 | -36.67% **▼** |
| cpu_stages | Tick/Total | p50 | 8.344 | 8.336 | -0.008 | -0.10% |
| cpu_stages | Tick/Total | p95 | 10.860 | 10.674 | -0.186 | -1.71% |
| cpu_stages | Tick/Unaccounted | p50 | 0 | 0 | 0 | +0.00% |
| cpu_stages | Tick/Unaccounted | p95 | 0 | 0 | 0 | +0.00% |
| cpu_stages | Vulkan/Acquire | p50 | 0.007 | 0.007 | -0.000 | -3.53% **▼** |
| cpu_stages | Vulkan/Acquire | p95 | 0.019 | 0.014 | -0.005 | -24.63% **▼** |
| cpu_stages | Vulkan/Present | p50 | 0.010 | 0.009 | -0.000 | -4.26% **▼** |
| cpu_stages | Vulkan/Present | p95 | 0.019 | 0.016 | -0.004 | -19.52% **▼** |
| cpu_stages | iterate | p50 | 8.344 | 8.336 | -0.008 | -0.10% |
| cpu_stages | iterate | p95 | 10.860 | 10.674 | -0.186 | -1.71% |
| gpu_passes | Deferred EntityId.view4294967297 | p50 | 0.133 | 0.134 | 0.001 | +0.37% |
| gpu_passes | Deferred EntityId.view4294967297 | p95 | 0.322 | 0.298 | -0.024 | -7.45% **▼** |
| gpu_passes | Deferred Forward Opaque.view4294967297 | p50 | 0.006 | 0.006 | -0.000 | -1.63% |
| gpu_passes | Deferred Forward Opaque.view4294967297 | p95 | 0.055 | 0.016 | -0.039 | -70.42% **▼** |
| gpu_passes | Deferred Forward Transparent.view4294967297 | p50 | 0.006 | 0.006 | 0.000 | +0.65% |
| gpu_passes | Deferred Forward Transparent.view4294967297 | p95 | 0.010 | 0.010 | 0.000 | +2.97% |
| gpu_passes | Deferred GBuffer.view4294967297 | p50 | 0.265 | 0.263 | -0.002 | -0.80% |
| gpu_passes | Deferred GBuffer.view4294967297 | p95 | 0.426 | 0.426 | 0.000 | +0.05% |
| gpu_passes | Deferred Light.view4294967297 | p50 | 0.741 | 0.736 | -0.004 | -0.59% |
| gpu_passes | Deferred Light.view4294967297 | p95 | 1.186 | 1.116 | -0.070 | -5.88% **▼** |
| gpu_passes | Deferred Skybox.view4294967297 | p50 | 0.007 | 0.007 | -0.000 | -0.91% |
| gpu_passes | Deferred Skybox.view4294967297 | p95 | 0.013 | 0.011 | -0.002 | -11.81% **▼** |
| gpu_passes | Directional Shadow Cascade 0.view0 | p50 | 0.029 | 0.027 | -0.002 | -6.43% **▼** |
| gpu_passes | Directional Shadow Cascade 0.view0 | p95 | 0.082 | 0.064 | -0.018 | -22.12% **▼** |
| gpu_passes | Point Shadow Faces.view0 | p50 | 0.292 | 0.228 | -0.064 | -22.03% **▼** |
| gpu_passes | Point Shadow Faces.view0 | p95 | 0.670 | 0.591 | -0.078 | -11.70% **▼** |
| gpu_passes | Postprocessing.view4294967297 | p50 | 0.004 | 0.004 | 0.000 | +8.26% **▲** |
| gpu_passes | Postprocessing.view4294967297 | p95 | 0.151 | 0.150 | -0.001 | -0.83% |
| gpu_passes | Presentation | p50 | 0.035 | 0.040 | 0.005 | +14.44% **▲** |
| gpu_passes | Presentation | p95 | 0.133 | 0.137 | 0.004 | +2.89% |
| gpu_passes | SSAO Pass.view4294967297 | p50 | 2.700 | 2.777 | 0.077 | +2.85% |
| gpu_passes | SSAO Pass.view4294967297 | p95 | 3.895 | 3.854 | -0.042 | -1.08% |
| render_counters | descriptorBindCount | p50 | 78.000 | 78.000 | 0 | +0.00% |
| render_counters | descriptorBindCount | p95 | 78.000 | 78.000 | 0 | +0.00% |
| render_counters | drawCount | p50 | 3.000 | 3.000 | 0 | +0.00% |
| render_counters | drawCount | p95 | 3.000 | 3.000 | 0 | +0.00% |
| render_counters | drawIndexedCount | p50 | 586.0 | 586.0 | 0 | +0.00% |
| render_counters | drawIndexedCount | p95 | 586.0 | 586.0 | 0 | +0.00% |
| render_counters | pipelineBindCount | p50 | 14.000 | 14.000 | 0 | +0.00% |
| render_counters | pipelineBindCount | p95 | 14.000 | 14.000 | 0 | +0.00% |
| gpu_memory | bufferMiB | p50 | 0.033 | 0.033 | 0 | +0.00% |
| gpu_memory | bufferMiB | p95 | 0.033 | 0.033 | 0 | +0.00% |
| gpu_memory | renderTargetMiB | p50 | 256.4 | 256.4 | 0 | +0.00% |
| gpu_memory | renderTargetMiB | p95 | 256.4 | 256.4 | 0 | +0.00% |
| gpu_memory | textureMiB | p50 | 8.760 | 8.760 | 0 | +0.00% |
| gpu_memory | textureMiB | p95 | 8.760 | 8.760 | 0 | +0.00% |
| gpu_memory | totalMiB | p50 | 270.7 | 270.7 | 0 | +0.00% |
| gpu_memory | totalMiB | p95 | 270.7 | 270.7 | 0 | +0.00% |
| gpu_memory | transientMiB | p50 | 0 | 0 | 0 | +0.00% |
| gpu_memory | transientMiB | p95 | 0 | 0 | 0 | +0.00% |
| gpu_memory | uploadMiB | p50 | 5.582 | 5.582 | 0 | +0.00% |
| gpu_memory | uploadMiB | p95 | 5.582 | 5.582 | 0 | +0.00% |
| process_memory | peakRssMiB | p50 | 287.9 | 316.3 | 28.430 | +9.88% **▲** |
| process_memory | peakRssMiB | p95 | 287.9 | 333.2 | 45.297 | +15.73% **▲** |
| process_memory | rssMiB | p50 | 232.8 | 314.1 | 81.289 | +34.91% **▲** |
| process_memory | rssMiB | p95 | 255.0 | 331.2 | 76.184 | +29.88% **▲** |

## Facts

- `frame.fps`: 118.0543259937893 -> 118.59670643975691
- `gpu_memory.heaps`: [{"allocationMiB":270.72681427001953,"blockMiB":320.3515625,"budgetMiB":13107.199999809265,"heapIndex":0,"usageMiB":320.3515625}] (unchanged)
- `gpu_memory.peak.bufferMiB`: 0.03345489501953125 (unchanged)
- `gpu_memory.peak.renderTargetMiB`: 256.3515625 (unchanged)
- `gpu_memory.peak.textureMiB`: 8.759765625 (unchanged)
- `gpu_memory.peak.transientMiB`: 0.0 (unchanged)
- `gpu_memory.peak.uploadMiB`: 5.58203125 (unchanged)
- `gpu_memory.supported`: True (unchanged)
- `process_memory.supported`: True (unchanged)
- `process_memory.unifiedMemory`: True (unchanged)

