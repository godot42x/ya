# RHI 图像 owner 收口 Session Checklist

> 更新时间：2026-08-16

## 每轮开工前

1. 先读：
   - `plan.md`
   - `todo.md`
   - `progress.md`
   - `feature_matrix.json`
2. 看工作区状态：
   - `git status --short`
3. 确认当前只推进一个最小切片，不混入无关重构。
4. 若要改 graph/runtime public API，先确认 `RenderTexture` 本体是否已经足够稳定。
5. 若要删旧接口，先确认没有重新造兼容桥。

## 每轮进行中

1. 公共语义先收口，再动 backend 内部细节。
2. 不新增 raw `IImageView` 业务入口。
3. 不为了少改几处而继续扩散 `RenderImage`。
4. 任何 owner 改动都必须检查 retained lifetime。

## 每轮收尾前

1. 至少跑一条最小构建验证；
2. 更新 `progress.md` 与 `todo.md`；
3. 复查是否把 graph/runtime public boundary 弄成新旧双真相；
4. 若形成稳定规则，后续考虑上收到 skill / AGENTS。
