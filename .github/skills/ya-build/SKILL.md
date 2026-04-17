---
name: ya-build
description: YA Engine 的 XMake 构建、目标选择、shader 生成与测试运行指南。
---

## 适用场景

- 构建失败、链接失败、目标找不到、编译参数不对
- 需要选择正确的 target 进行 build / run / test
- 需要刷新 `compile_commands.json` 或重跑 shader 生成
- 需要判断应该用 `make` 包装命令还是直接用 `xmake`

## 核心规则

1. 只使用 XMake；不要引入 CMake。
2. 默认目标是 `ya`，示例目标在 `Example/` 下。
3. shader 生成通过 `xmake ya-shader`，不要手改 `Engine/Shader/*/Generated/*`。
4. 测试使用 GoogleTest 目标，不要自己发明另一套测试入口。

## 常用命令

### Makefile 包装（优先）

```bash
make r t=HelloMaterial
make b t=HelloMaterial
make f=true r t=HelloMaterial
make test t=ya r_args="Suite.Test"
make cfg
```

### 直接使用 XMake

```bash
xmake
xmake b TargetName
xmake run TargetName
xmake l targets
xmake f -m debug -y
xmake ya-shader
xmake b ya-testing && xmake r ya-testing --gtest_filter=Suite.Test
```

## 何时用哪种入口

1. 日常构建 / 运行 / 刷新 compile commands，优先用 `make` 包装命令。
2. 需要精确控制某个 xmake 子命令时，直接用 `xmake`。
3. 需要刷新工作区构建配置时，用 `make cfg`。
4. 需要重建 shader 头时，用 `xmake ya-shader`。

## 当前仓库锚点

- `Makefile`：封装 `b / r / cfg / test`
- `xmake.lua`：全局规则、`compile_commands.autoupdate`
- `Xmake/task.lua`：`xmake cpcm`、`xmake vscode`
- `Engine/Shader/Shader.xmake.lua`：shader 生成入口
- `Test/xmake.lua`：测试目标定义
- `Example/`：可运行示例目标

## 常见排查顺序

### 1. 目标找不到

1. 先运行 `xmake l targets` 确认目标名。
2. 再确认目标是不是 example、主程序还是 `*-testing`。
3. 若是 VS Code 启动项问题，再联动看 `vscode` skill。

### 2. 编译通过但运行入口不对

1. 检查 `make r t=<target>` 或 `xmake run <target>` 的 target 是否正确。
2. 确认 `launch.json` 中程序路径是否仍匹配当前输出目录。
3. 若是示例程序，确认目标名是否来自 `Example/`。

### 3. shader 相关报错

1. 先确认是不是生成头过期。
2. 运行 `xmake ya-shader`。
3. 若仍失败，回看 `Engine/Shader/Shader.xmake.lua`、`slang_gen_header.py`、`glsl_gen_header.py`。
4. 不要直接编辑 `Generated/*.slang.h` 或 `Generated/*.glsl.h`。

### 4. IntelliSense / clangd 不对

1. 先刷新 `compile_commands.json`：`make cfg` 或 `xmake cpcm`。
2. 再检查 VS Code 是否读取了正确文件。
3. 这类问题通常和 `vscode` skill 一起看。

### 5. 测试运行失败

1. 确认目标是 `$(t)-testing` 或 `ya-testing`。
2. 确认 `--gtest_filter` / `r_args` 写法正确。
3. 若只是单测不过，构建链路没问题时再转去具体模块 skill。

## 相关 skills

- `vscode`：处理任务、launch 配置、compile_commands 与调试链路
- `render-arch`：改到 shader、RenderRuntime、后端边界时一起看
- `resource-system`：资源链路改动引起的编译或生成问题时一起看
- `material-flow`：材质 shader / UBO 变更引起的构建问题时一起看
- `cpp-style`：修编译错误同时保持风格与最小改动时一起看
- `debug-review`：构建修复后做提交前自检时一起看

## 不建议的方向

1. 不要在仓库里引入 CMake 文件或 CMake 专用说明。
2. 不要直接修改生成文件来“修构建”。
3. 不要把 VS Code 专属问题误判成构建系统问题。
4. 不要在一次修构建时顺手做无关重构。

## 退出条件

- 用户知道该用哪个 target、哪个命令入口
- 构建 / 运行 / 测试 / shader 生成路径至少有一条明确可用
- 问题已收敛为构建配置、生成链、目标选择，或已转交给更合适的 skill