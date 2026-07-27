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
2. 当前工作区优先通过 `python3 Script/ya.py` 组织 build / run，`.vscode/tasks.json` 应围绕这条主路径，`.vscode/launch.json` 直接调试产出的 `ya-runtime`。
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

- `ya-build`：构建入口、xmake 参数、测试入口与 shader 生成要一起看
- `debug-review`：当问题表现为断点失效、调试配置异常时一起看
- `cpp-style`：需要联动 clangd / IntelliSense 诊断代码风格问题时可一起看

## 当前工作区锚点

- `.vscode/tasks.json`：`ya cfg`、`ya build project`、`ya build editor project`
- `.vscode/launch.json`：`YA: Debug Runtime Project`、`YA: Debug Editor Project`、Windows 通用附加/启动项
- `.vscode/settings.json`：拼写词典、Lua 诊断、shader lint 等编辑器设置
- `xmake.lua`：启用了 `plugin.compile_commands.autoupdate`
- `Xmake/task.lua`：提供 `xmake cpcm` 任务来重建 compile commands

## compile_commands 刷新

优先使用现有工程方式：

```bash
python3 Script/ya.py cfg
```

或直接使用 XMake：

```bash
xmake project -k compile_commands
xmake cpcm
```

排查要点：

1. 若 clangd 语义不对，先确认根目录 `compile_commands.json` 是否已刷新。
2. 若 VS Code 仍读取旧数据，再检查 `.vscode/compile_commands.json` 是否需要同步。
3. 若调试输入框或文件选择失效，先检查 `rioj7.command-variable` 是否安装。

## 调试建议

1. Windows / MSVC 默认使用 `cppvsdbg`。
2. 若目标需要先构建，优先复用 `ya build project` / `ya build editor project` 这类 `ya.py` 任务。
3. 调试程序路径优先跟随当前 `build/.../ya-runtime` 产物，不要再为 project 层维护一套 legacy target 推断。

## 不建议的方向

1. 不要引入 `ms-vscode.cmake-tools` 作为主推荐。
2. 不要手写或长期维护重复的 project/target 推断脚本，优先复用已有 `ya.py` / `xmake` 流程。
3. 不要把与仓库无关的全局用户设置写进工作区配置。

## 退出条件

- VS Code 内构建、启动、附加调试至少有一条路径可用
- `compile_commands.json` 来源清晰且可刷新
- 推荐的扩展和配置与当前仓库实际文件一致
