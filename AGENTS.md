# YA Engine Codex Instructions

This repository already maintains detailed guidance in [`.github/copilot-instructions.md`](C:\Users\norm\1\craft\ya\.github\copilot-instructions.md). Treat that file as the primary project instruction source when working in Codex.

## Project

- Name: YA Engine
- Type: C++ game engine
- Build system: XMake
- Render backends: Vulkan + OpenGL

## First Source Of Truth

When task-specific guidance is needed, read [`.github/copilot-instructions.md`](C:\Users\norm\1\craft\ya\.github\copilot-instructions.md) first, then follow the relevant skill document under [`.github/skills`](C:\Users\norm\1\craft\ya\.github\skills).

Available project skills:

- `SOUL`: [`.github/skills/soul/SKILL.md`](C:\Users\norm\1\craft\ya\.github\skills\soul\SKILL.md)
- `BUILD`: [`.github/skills/build/SKILL.md`](C:\Users\norm\1\craft\ya\.github\skills\build\SKILL.md)
- `MATERIAL_FLOW`: [`.github/skills/material-flow/SKILL.md`](C:\Users\norm\1\craft\ya\.github\skills\material-flow\SKILL.md)
- `RENDER_ARCH`: [`.github/skills/render-arch/SKILL.md`](C:\Users\norm\1\craft\ya\.github\skills\render-arch\SKILL.md)
- `CPP_STYLE`: [`.github/skills/cpp-style/SKILL.md`](C:\Users\norm\1\craft\ya\.github\skills\cpp-style\SKILL.md)
- `DEBUG_REVIEW`: [`.github/skills/debug-review/SKILL.md`](C:\Users\norm\1\craft\ya\.github\skills\debug-review\SKILL.md)
- `VSCODE`: [`.github/skills/vscode/SKILL.md`](C:\Users\norm\1\craft\ya\.github\skills\vscode\SKILL.md)

## Skill Selection

- Unclear request: start with `SOUL`
- Build, run, target selection, compiler errors: `BUILD`
- ECS to material to render pipeline data flow: `MATERIAL_FLOW`
- Render pipeline, backend boundaries, material architecture: `RENDER_ARCH`
- C++ additions, refactors, style, ownership: `CPP_STYLE`
- Crash investigation, log analysis, review: `DEBUG_REVIEW`
- VS Code tasks, launch, settings, clangd: `VSCODE`

Priority when multiple skills apply:

`BUILD > VSCODE > MATERIAL_FLOW > RENDER_ARCH > CPP_STYLE > DEBUG_REVIEW > SOUL`

## Build Rules

- Use XMake, not CMake
- Prefer reading the repository `Makefile` wrapper before inventing commands
- Common commands:
  - `xmake`
  - `xmake run <TargetName>`
  - `xmake clean`
  - `xmake config`

## Code Rules

- Follow existing interface abstractions
- Prefer smart pointers over raw `new` and `delete`
- If lifecycle is not fully understood, do not introduce manual ownership
- Class member declarations should appear before methods when adding new classes

Naming conventions:

- Types: `PascalCase`
- Private members: `_memberName`
- Public members: `memberName`
- Locals and functions: `camelCase`
- Constants: `UPPER_SNAKE_CASE`

## Engine Notes

- `Render2D::drawTexture()` uses top-left origin coordinates
- `IRender` must be initialized before dependent systems use it
- Avoid resource recreation during frame recording; defer unsafe recreate work to the next frame

## Logging

Prefer existing YA logging/assert macros:

- `YA_CORE_TRACE`
- `YA_CORE_DEBUG`
- `YA_CORE_INFO`
- `YA_CORE_WARN`
- `YA_CORE_ERROR`
- `YA_CORE_ASSERT`

## Change Discipline

- Do not add extra markdown documents unless the user explicitly asks
- Keep comments minimal and necessary
- Commit messages should use prefixes like `[xxx] message`
- Multi-module prefixes are allowed, for example `[rhi x material] ...`
