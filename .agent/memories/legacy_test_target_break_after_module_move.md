# 模块迁移后遗留测试 target 失效

适用场景：

- 全量 `xmake b` 突然报某个测试 target 编译失败，且错误与当前工作无关
- 错误是“include 头文件不存在 / 符号已删除 / 类型已重构”，但该头文件/符号在别处已迁移

## 2026-08-22 清理结论

一次 RetainedResource 重构验收时，发现 `ya-gui-closure-test`、`ya-testing`、
`reflects-generator-test` 等多个 target 因**历史模块迁移未同步测试**而编译失败。
这类问题不是当前改动引入的，而是“把某个模块从 A 迁到 B，但测试文件没跟着改”的遗留。

常见形态：

1. **测试 include 失效**：`Render2DClipTest.cpp` 引 `GUI/Draw2D/Render2D.h`，
   但 Render2D 已迁到 `Framework/Render/Render2D/`（commit `9c24c071`）。
   修复 = 把测试移到正确的 render test target，并修 include 路径。
2. **测试引用的 API 被折回/删除**：`AppLifecycleTest.cpp` 用 `AppLifecycle::*`，
   但该 namespace 已折回 `App` 类（commit `97d09107`）。修复 = 通过
   `AppModuleTestAccess`（friend 访问器）转发到新 private 方法。
3. **测试引用的类型被收敛**：`RenderImage` 被 `ImageResource` / `RenderTexture`
   取代。修复 = 按新 owner 类型改写（`RenderTexture::wrap`、`ImageResource` 默认构造）。
4. **字段可见性变化**：`UIPanel::_color` 变 protected，测试直接访问失败。
   修复 = 改用 `getColor()`。
5. **生成头无条件 include**：`reflects` 的 `common.generated.h` 无条件 include，
   但生成规则被注释掉。修复 = 加 `__has_include` 保护（与 `game_object.h` 一致）。
6. **枚举类型收紧**：`MouseButton*Event(0)` 用 int 构造，构造函数改 `explicit(EMouse::T)`。
   修复 = 用 `EMouse::Left` / `EMouse::fromSDLMouseButton()`。

## 约定

- 做“模块迁移 / 类型收敛 / API 折回”这类改动时，**必须同步 grep 测试目录**
  （`Engine/Test/`）里对该模块的引用，避免留下失效 target。
- 全量构建前先跑 `xmake b` 一次，逐 target 收敛，而不是只看自己改动的 target。
- 测试失效若与当前工作无关，先确认它是历史遗留（`git log` 该测试文件 / 被引头文件），
  再决定“迁移到正确 target”还是“更新 API”，不要顺手删测试或改测试语义。
