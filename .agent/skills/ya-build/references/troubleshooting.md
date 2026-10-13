## 常见排查顺序

### 1. 目标找不到

1. 先运行 `xmake l targets` 确认 xmake 目标名。
2. 再确认当前要跑的是 `ya-runtime + project=...`，还是 `*-testing`。
3. 若是 VS Code 启动项问题，再联动看 `vscode` skill。

### 2. 编译通过但运行入口不对

1. 优先检查是否应该使用 `python3 Script/ya.py run --project <path>`。
2. 确认 `launch.json` 中程序路径是否仍匹配当前输出目录。

### 3. shader 相关报错

1. 先确认是不是生成头过期。
2. 运行 `xmake ya-shader`。
3. 若仍失败，回看 `Engine/Shader/Shader.xmake.lua`、`slang_gen_header.py`。
4. 不要直接编辑 `Generated/*.slang.h`。

### 4. IntelliSense / clangd 不对

1. 先刷新 `compile_commands.json`：`python3 Script/ya.py cfg`。
2. 再检查 VS Code 是否读取了正确文件。
3. 这类问题通常和 `vscode` skill 一起看。

### 5. 测试运行失败

1. 先跑 `make test` 拿全貌；group 遇红即停，所以再 `xmake r <target>` 单独复现。
2. 确认目标是 `$(t)-testing` 或 `ya-testing`，`--gtest_filter` 写法正确。
3. 断言失败先分清三类：框架 bug / 框架变更后用例没跟上 / 用例本身无意义（例如断言一个
   被注释掉的代码生成器产物、`EXPECT_EXIT` 里再起线程、依赖平台真的最小化窗口）。见
   `../../../memories/stale_test_assertions_after_contract_change.md`。
4. 构建没问题时再转去具体模块 skill。


