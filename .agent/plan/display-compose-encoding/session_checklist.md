# Session Checklist

## 开工

1. 读 plan.md 的 phase 边界，确认这一轮只推进一个 checkpoint。
2. 确认 display image 的不变量没有被破坏：
   grep -n "display = postprocess" Engine/Source/Framework/Render/Render3D
3. 确认 present 没有重新引入 grading：
   grep -n "kDisplayComposeState" Engine/Source/Framework/Render/Render3D/Services/PresentationGraphService.cpp

## 收尾

1. 门禁：python3 Script/automation/render/run_display_compose_parity.py
   （viewport 与 presentation 必须逐字节相同）
2. xmake r ya-render-3d-test（期望 175/175）
3. 编辑器冒烟：python3 Script/ya.py run-editor ... --screenshot=...
   期望 exit 0、无 error/assert、截图与上一次逐字节相同（F1 不改变编辑器输出）
4. 若改了 View 的 finalize 行为，viewport 基线 md5 必须同步更新并记录在 progress.md

