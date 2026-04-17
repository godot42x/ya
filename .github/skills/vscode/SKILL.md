---
name: ya-vscode
description: VS Code 工作区配置、XMake 任务、调试启动项与 IntelliSense 排查。
---

## 适用场景

- 配置 `.vscode/tasks.json`、`launch.json`、`settings.json`
- 设置或修复 VS Code 内的构建 / 调试流程
- 排查 `compile_commands.json`、clangd、cpptools、断点无效等问题
- 推荐与当前工作区实际配置相匹配的扩展

## 核心规则

1. 构建系统只有 XMake；不要推荐 CMake / MSBuild 作为主流程。
2. 当前工作区已有 `.vscode/tasks.json` 的 `xmake build` 任务，以及 `.vscode/launch.json` 中基于 `cppvsdbg` 的 attach / launch 配置；新增配置应与这套方式保持一致。
3. `compile_commands.json` 由 XMake 生成，不手写。
4. 路径优先使用 `${workspaceFolder}`，不要写用户本机绝对路径。

## 推荐扩展

| 扩展 ID | 用途 |
|---|---|
| `ms-vscode.cpptools` | Windows 下 `cppvsdbg` 调试 |
| `llvm-vs-code-extensions.vscode-clangd` | clangd 语义分析 |
| `xmake-vscode.xmake` | XMake 集成 |
| `rioj7.command-variable` | 当前 `tasks.json` / `launch.json` 里用到了 `extension.commandvariable.*` 命令 |

## 相关 skills

- `build`：目标选择、xmake 参数、测试入口与 shader 生成要一起看
- `debug-review`：当问题表现为断点失效、调试配置异常时一起看
- `cpp-style`：需要联动 clangd / IntelliSense 诊断代码风格问题时可一起看

## 当前工作区锚点

- `.vscode/tasks.json`：默认构建任务 `xmake build`
- `.vscode/launch.json`：`Attach EXE`、`debug EXE MSVC`、`debug EXE MSVC with build`
- `.vscode/settings.json`：拼写词典、Lua 诊断、shader lint 等编辑器设置
- `xmake.lua`：启用了 `plugin.compile_commands.autoupdate`
- `Xmake/task.lua`：提供 `xmake cpcm` 任务来重建 compile commands

## compile_commands 刷新

优先使用现有工程方式：

```bash
make cfg
```

或直接使用 XMake：

```bash
xmake project -k compile_commands
xmake cpcm
```

排查要点：

1. 若 clangd 语义不对，先确认根目录 `compile_commands.json` 是否已刷新。
2. 若 VS Code 仍读取旧数据，再检查 `.vscode/compile_commands.json` 是否需要同步。
3. 若调试输入框或 target 选择失效，先检查 `rioj7.command-variable` 是否安装。

## 调试建议

1. Windows / MSVC 默认使用 `cppvsdbg`。
2. 若目标需要先构建，复用已有 `preLaunchTask: "xmake build"`。
3. 目标程序路径优先跟随当前 `build/windows/x64/debug/<target>.exe` 约定，不要另起一套目录推断。

## 不建议的方向

1. 不要引入 `ms-vscode.cmake-tools` 作为主推荐。
2. 不要手写或长期维护重复的 target 列表脚本，优先复用已有 `xmake` / `command-variable` 流程。
3. 不要把与仓库无关的全局用户设置写进工作区配置。

## 退出条件

- VS Code 内构建、启动、附加调试至少有一条路径可用
- `compile_commands.json` 来源清晰且可刷新
- 推荐的扩展和配置与当前仓库实际文件一致