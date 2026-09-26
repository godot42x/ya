# Session Checklist

## 开工

1. 读本目录 plan.md 当前 phase、progress.md 最近一轮和 feature_matrix.json 未完成项。
2. 读 AGENTS.md、.agent/plan/AGENTS.md，确认公开头唯一物理位置、Slang-only、资源生命周期和提交门禁。
3. git status --short；保留用户/其他 agent 的脏改动，禁止 blanket stage。
4. 只核对当前 phase 的调用方：
   - P0/P1：rg -n "Render2D::|FQuadRender|Sprite2D" Engine/Source Engine/Shader
   - P2：rg -n "CameraComponent|FreeCamera|orthographic" Engine/Source
   - P3/P4：rg -n "Sprite2DComponent|SceneSnapshot|SceneViewDesc|RenderFrameData" Engine/Source
   - P5/P6：rg -n "EditorViewProducer|EditorGameUIPreview|GameUIHost|UIFrameSnapshot" Engine/Source
5. P0 额外核对：Render3D 是否 include GUI/Compose、FramePacket 是否携带 UIFrameSnapshot、
   Forward/Deferred bloom/finalize 的 graph 输入、CameraComponent 是否在 getter 中读/写 owner。
6. 明确本轮唯一可验收目标；若要跨 phase，先更新 plan，不在代码里顺手扩 scope。

## 实施中

1. 先改公开契约/数据结构，再改 extraction，再改 recording；不要从 shader 特例倒推 Scene 模型。
2. 每次新增 GPU 资源都确认 safe point、flight slot、retire/submit 生命周期。
3. 同一 Scene 多 View 的 shared snapshot 与 view-owned order 必须分开；禁止把 view camera 写回 shared snapshot。
4. UI pipeline 不 include Scene/ECS；World2D pipeline 不 include WidgetTree/UIFrameSnapshot。
5. 不引入 Transform2D、Camera2D、中心 bus、第二个 app loop 或 IRenderRuntimeServices。
6. 不把 World2D 实现成第三个 ISceneViewFamilyRenderer；它必须落在 active Forward/Deferred graph。
7. 不预设 shader/Batch 拆分数量；先完成 capability matrix 和资源生命周期表。

## 收尾

1. 更新 progress.md：完成项、保留项、偏离项、失败测试和下一步。
2. 只把本 phase 代码、测试和计划工件一起提交，提交前检查 git diff --stat 与 git diff --check。
3. 按 phase 执行构建/测试：xmake ya-shader（涉及 shader 时）、对应 xmake b、对应 xmake r ya-testing、
   runtime/editor smoke，以及涉及 UI compose 时的 GUI closure/GPU parity。
4. 用 rg 做删除审计：旧 mixed shader、旧 texture flag、Transform2D、Camera2D、重复 snapshot owner。
5. 用依赖审计确认 ya-render-3d 不再 include GUI/Compose 或 UIFrameSnapshot；Game UI compose 由
   GameRuntime/GameEditor 明确拥有。
6. 只有在行为闭环可运行时才把 feature_matrix.json 的 status 改为 verified。
