# 断言层的测试过期：`make test` 里的红不都是回归

适用场景：

- 跑全量测试时出现一批红，但构建通过、错误信息看着像“框架坏了”
- 需要判断某条用例该修框架、该改用例，还是该删

编译期的过期在 `legacy_test_target_break_after_module_move.md`；这篇是**断言层**：
用例引用的 API 还在，所以能编过，但断言的是一个已经变掉的契约。

## 三类归属（2026-09-23 全量清理结论）

| 类型 | 判据 | 处置 |
| --- | --- | --- |
| 框架变更，用例没跟上 | 框架里有明确注释/设计说明；另有其它调用点已按新契约写 | 改用例，把新契约写进注释 |
| 用例本身无意义 | 断言依赖被禁用的生成步骤、`EXPECT_EXIT` 里再建线程、需要平台真的最小化窗口 | 删掉该断言，或把 target 移出 `test` group 并写明原因 |
| 框架 bug | 契约仍然成立、且有生产调用点会被影响 | 修框架（本轮未出现这类） |

## 本轮修掉的具体形态

1. **`engine.panel` 不再绘制**：widgets 阶段把“画出来的矩形”拆成 `engine.border`
   （`UIBorder` + `FPanelStyle`），`UICanvasPanel` 变成纯布局宿主（头注释：*does not
   paint*）。三个用例仍在 `engine.panel` 上找 draw item / 写 `_color` → 改用 `engine.border`。
2. **剪贴板不是 per-tree 的**：`GUIWindowManager` 给每个 session 的 tree 绑了 OS 剪贴板
   （真窗口就是真剪贴板客户端），所以最后一个写者对每个窗口可见；tree-local 字段只服务
   无 hook 的 tree。断言“A 写、B 读不到”的用例拆成一条显式声明进程级语义的用例。
3. **坐标/下标定位控件**：`assetPathField` 之类按 `children[i]` 取控件，编辑器把按钮从
   path row 移到独立 button row 后全部变成 `nullptr`，`ASSERT_NE(nullptr)` 直接报红且看不出
   原因。改成按 authored key 查找：DSL 控件在 `_stableKey`，直构控件在 `_name`。
4. **默认参数翻转**：`visitAllProperties(obj, visitor, bool recursive = true)` 为了修遍历顺序
   改成默认 `false`，三个“想递归”的调用点没写参数 → 断言 3/2/2 个属性却只拿到 1 个。
   处置是**去掉默认值**（名字说 all，就该在调用点表态），而不是让用例去迁就默认值。
5. **依赖被禁用的生成器**：`reflects-generator-test` 断言生成器注册了 `Person/Vehicle`，但
   生成规则自 `aea69086a` 起被注释，工具链（`python` + `clang` 模块）也不在 → 10/11 用例
   注定红。移出 `test` group。
6. **`EXPECT_EXIT` + 线程**：gtest death test 会 fork，子进程里再 `std::thread`，在父进程已有
   线程（异步 worker）时 abort → “died but not with expected exit code” 随机红。去掉
   subprocess 包装，保留场景。
7. **平台真的最小化窗口**：见下方环境坑。

## 环境坑：SDL minimized 标志在测试宿主里会残留

`RHISurfaceContext.ExtraWindowUnpresentableDoesNotBlockPrimaryPresent` 需要平台真的走完
minimize → restore。本机（macOS，SDL3）实测：

- 单独跑：`minimize()` 之后 `isMinimized()` 仍为 false（窗口管理器没真的最小化）→ 用例
  自己 skip。
- 全量跑：`isMinimized()` 为 true，但 `SDL_RestoreWindow()` + `SDL_PumpEvents()` 之后**仍然是
  true**（没有 deminimize 事件来清标志），此时 `isPresentable()` 正确返回 false —— 卡在
  `_window->isMinimized()` 这一项，`size` 已经是 200x150。

所以 `isPresentable()` 的门（`!isMinimized() && size>0 && surfacePresentable`）没问题，
是宿主给不出平台事件。处置：用例在每个方向上都显式检查标志并 skip 原因，让红只可能来自
surface 路径。**不要**为了让它变绿去改 `isPresentable()` 的判据。

## 约定

- 断言过期时优先改**断言**而不是改框架；只有当生产调用点会受影响时才动框架。
- 删/改用例必须在提交信息里说明“它验证的问题与现状如何不符”，不要静默放宽断言。
- 新用例不要用子节点下标定位控件、不要用 `__FILE__` 加固定 `../..` 读源码（见
  `../skills/ya-build/SKILL.md` 的测试源码布局一节）。
