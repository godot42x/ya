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

窗口和布局必须固定，否则视口尺寸会漂，逐像素对比没有意义。`--width=1024 --height=768`，`--layout-overrides` 指向一个空目录，这样走出发厂 `Level.json`，而不是本机 `Workspace.json` 里的自定义分割。出厂布局下视口是 570×370。

```bash
mkdir -p /tmp/ya-ibl-layout-empty
python3 Script/ya.py run-editor --project Example/HelloMaterial/HelloMaterial.yaproject -- \
    --exit-after-frame=1500 --screenshot-frame=1500 \
    --screenshot=/tmp/ibl-check.png --screenshot-target=editor \
    --width=1024 --height=768 \
    --layout-overrides=/tmp/ya-ibl-layout-empty \
    --editor-camera-pos=12,12,10 --editor-camera-rot=-9,-39,0 \
    --log-level=warn --log-detail-level=error
```

`--screenshot-target=editor` 不是合法取值，解析会忽略并落到默认的 viewport。不要改成 presentation。

## 已知画面

H5 之前（`f3d0c1a5` 一带）的同尺寸截图里，球体有竖向条纹，地面有网点。这是当时的画面，不是窗口缩放，也不是 debug / release 的差别。H5 之后的球体反射连续。对照时以 H5 之后的 `/tmp/ibl-head-matched.png` 为同尺寸基线。

## 帧数规则

1. 对 environment preprocess / offscreen resolve / 异步资源完成有依赖的问题，短帧 smoke 不算验证通过。
2. 这类问题至少跑到确实完成环境预处理的帧数再下结论。
3. `--exit-after-frame=1500` 不会自动把截图推迟到 1500 帧；若要晚帧抓图，必须同步设置 `--screenshot-frame=1500`。

## 常见误判

1. 把 Phong 物体当成 PBR 环境反射主观察点。
2. 自动化截图和人工编辑器观察冲突，却继续把自动化结果当唯一真相。
3. 只看“资源 ready / descriptor 已更新”的日志，就默认最终视觉一定正确。
4. 一边换提交，一边换观察点或机位，导致对照口径漂移。
