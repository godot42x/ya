```skill
---
name: ya-vscode
description: VS Code 工作区配置、任务、调试与插件管理
---

## 适用场景

- 配置 `.vscode/tasks.json`、`launch.json`、`settings.json`
- 设置调试启动项（xmake 目标 attach / launch）
- 推荐或安装 VS Code 扩展
- 排查 IntelliSense / clangd / compile_commands.json 相关问题
- 配置代码格式化、代码片段、工作区设置

## 核心规则

1. 构建任务始终调用 XMake，不引入 CMake/MSBuild。
2. 调试配置优先使用 `cppvsdbg`（Windows MSVC）或 `cppdbg`（GDB/LLDB）。
3. `compile_commands.json` 由 `xmake project -k compile_commands` 生成，不手写。
4. 所有路径使用 `${workspaceFolder}` 变量，不写绝对路径。

## 推荐扩展

| 扩展 ID | 用途 |
|---|---|
| `ms-vscode.cpptools` | C++ IntelliSense / 调试 |
| `llvm-vs-code-extensions.vscode-clangd` | clangd 语义分析（二选一）|
| `xmake-vscode.xmake` | XMake 集成 |
| `ms-vscode.cmake-tools` | 仅在需要 CMake 目标时使用 |

## 常用任务模板（tasks.json）

```json
{
  "label": "xmake build target",
  "type": "shell",
  "command": "xmake b ${input:target}",
  "group": { "kind": "build", "isDefault": true },
  "problemMatcher": "$msCompile"
}
```

## 调试配置模板（launch.json）

```json
{
  "name": "Launch Target",
  "type": "cppvsdbg",
  "request": "launch",
  "program": "${workspaceFolder}/build/windows/x64/debug/${input:target}.exe",
  "cwd": "${workspaceFolder}",
  "preLaunchTask": "xmake build target"
}
```

## compile_commands 刷新

```bash
xmake project -k compile_commands
```

在 `settings.json` 中指向生成路径：
```json
{
  "clangd.arguments": ["--compile-commands-dir=${workspaceFolder}"]
}
```

## 退出条件

- 所需配置文件已正确生成，构建/调试流程可在 VS Code 内端到端运行
```
