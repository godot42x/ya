# IBL Visual Regression Baseline

适用场景：

- IBL / skybox / environment prefilter / irradiance / BRDF LUT 一类视觉回归
- “资源看起来 ready 了，但画面不对”
- 需要区分“真正没修好”和“验证口径本身不可靠”

## 观察基线

```text
scene   = Example/HelloMaterial/Content/Scenes/HelloMaterial.scene.json
target  = PBR_Sphere_5_0
camera  = pos(12,12,10) rot(-9,-39,0)
signal  = 球体表面是否能看到天空盒/环境倒影
```

不要把 `Cube_2_0` 这类 Phong 物体当成主观察点。

## 推荐验证顺序

1. 先固定场景、观察物、机位、抓图帧。
2. 先做人工编辑器冒烟，再决定自动化截图是否可信。
3. 若和 `origin/main` 对比，必须保持同一 scene / target / camera / frame gate。

## 推荐命令模板

```bash
make r t=HelloMaterial r_args="--exit-after-frame=1500 --screenshot-frame=1500 --screenshot-target=editor --editor-camera-pos=12,12,10 --editor-camera-rot=-9,-39,0 --log-level=warn --log-detail-level=error"
```

若需要落盘截图，再补：

```bash
--screenshot=/tmp/ibl-check.png
```

## 帧数规则

1. 对 environment preprocess / offscreen resolve / 异步资源完成有依赖的问题，短帧 smoke 不算验证通过。
2. 这类问题至少跑到确实完成环境预处理的帧数再下结论。
3. `--exit-after-frame=1500` 不会自动把截图推迟到 1500 帧；若要晚帧抓图，必须同步设置 `--screenshot-frame=1500`。

## 常见误判

1. 把 Phong 物体当成 PBR 环境反射主观察点。
2. 自动化截图和人工编辑器观察冲突，却继续把自动化结果当唯一真相。
3. 只看“资源 ready / descriptor 已更新”的日志，就默认最终视觉一定正确。
4. 一边换提交，一边换观察点或机位，导致对照口径漂移。
