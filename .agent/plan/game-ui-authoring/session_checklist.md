# Session Checklist

## 开工

1. 读本目录 plan.md 的「当前边界」，确认这一轮只推进一个 phase。
2. git status：GameEditor 可能有并发写入者，禁止 blanket stage。
3. 确认 Scene / GameUIHost / EditorUIDesignerSession 的当前调用方：
   rg -n "SceneWidgetEntry|mountSceneAutoMountEntries|documentPath" Engine/Source

## 收尾

1. xmake b ya-scene-core ya-gui-widgets ya-game-runtime ya-game-editor ya-testing
2. xmake r ya-testing --gtest_filter='Scene*:GameUIHost*:EditorUIDesigner*'
3. 运行时冒烟：python3 Script/ya.py run --project Example/HelloMaterial/HelloMaterial.yaproject -- --exit-after-frame=90
4. 确认没有重新引入内联文档、没有第二份文档缓存。

