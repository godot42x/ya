# Session Checklist

## 开工

1. 读 plan.md 的 phase 边界，确认这一轮只推进一个 checkpoint。
2. 确认 display image 的不变量没有被破坏：
   grep -n "display = postprocess" Engine/Source/Framework/Render/Render3D
3. 确认 present 没有重新引入 grading：
   grep -n "kDisplayComposeState" Engine/Source/Framework/Render/Render3D/Services/PresentationGraphService.cpp
4. 确认窗口底板的声明还在宿主侧：
   grep -rn "bCopyViewDisplayImage" Engine/Source
   （默认 true；只有编辑器那种「chrome 铺满 surface」的宿主才置 false）
5. 确认判据不是 AppState（编辑器的 Play 也是 Runtime 模式）：
   grep -rn "fillsPrimarySurface\|presentsViewDisplayImage" Engine/Source

## 收尾

1. 门禁：python3 Script/automation/render/run_display_compose_parity.py
   （viewport 与 presentation 必须逐字节相同）
2. xmake r ya-render-3d-test（期望 175/175）
3. 编辑器冒烟：python3 Script/ya.py run-editor ... --screenshot-target=presentation --screenshot=...
   期望 exit 0、无 error/assert。截图比对必须排除 Frame Inspector HUD 的实时计时区域
   （x 1140..1240 / y 735..776），否则每次运行都会漂移；排除后应逐字节相同。
4. 若改了 View 的 finalize 行为，viewport 基线 md5 必须同步更新并记录在 progress.md
5. 跑宽滤镜时排除 WidgetTreeTest.SystemLayersCannotBeDetached：该用例故意 detach 系统
   layer，而 YA_CORE_ASSERT 会 PLATFORM_BREAK()，所以它必然以 SIGTRAP 打死整个测试进程
   （HEAD 上就如此，与 render 路径无关）。
