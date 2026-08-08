# 模块拆分时 sed 误删函数导致运行时跳 0x0 崩溃

> 2026-08-09，EnvironmentLightingProcessor 拆分 TerrainProcessor 时发生。

## 现象

- 构建、链接、350 测试全部通过；但运行时首帧 skybox 源加载后崩溃：
  `EXC_BAD_ACCESS (code=1, address=0x0)`，`frame #0: 0x0000000000000000`，
  栈完全损坏（bt 无更多帧）。
- 用 lldb 断点逐步定位：崩溃在 `resolvePendingEnvironmentLighting` 内
  `findFirstSceneSkyboxState(scene)` 调用（`symbol stub` 跳 0）。
- `nm libya-render-3d.dylib | rg findFirstSceneSkyboxState` →
  `U`（**undefined**）——定义被误删，但**调用点还在**，动态链接 stub 跳 0。
- 为什么编译/测试没抓到：调用点编译通过（符号缺失只在链接期才报错，但
  macOS dylib 默认允许 undefined symbol 链接成功），单测没覆盖该路径。
- ASan 下不崩溃（heap 布局变化掩盖）；stash 对比基线正常。

## 根因

用 `sed -i 'N,Md'` 删除大代码块时，**前一次删除已经改变行号**，第二次删除
仍按旧行号计算，误删了目标块之外的函数（`findFirstSceneSkyboxState`）。

## 预防

1. 大块删除**不要用 sed 行号**；用 `apply_patch` 或按**文本锚点**（函数签名）
   删除，每次删除后重新 grep 行号。
2. 删除后**核对函数清单**：对比 HEAD 与当前文件的成员函数集合
   （`grep -oE '^.*ClassName::[A-Za-z_]+' | sort -u` 后 `comm`），确保只缺
   预期删除的函数。
3. macOS dylib 未定义符号不会在链接期报错；拆分后检查
   `nm <dylib> | rg ' U .*ClassName'` 看是否有意外 undefined 成员函数。
4. 运行时跳 0x0 + 栈损坏 + ASan 正常 = 高度怀疑跨模块符号/布局问题，
   先查 `nm` 符号表再查代码。
