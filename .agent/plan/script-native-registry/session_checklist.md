# Session Checklist

## 开工

1. 读 `plan.md` §2 决策与 §4 当前 checkpoint，读 `todo.md`。
2. 确认 xmake mode 与测试基线（当前 `ya.py test --target ya` 全量绿数）。
3. 涉及 `ScriptBindings` / 反射注册的改动先跑过滤测试 `--filter '*Script*'`。

## 收尾

1. 全量 `python3 Script/ya.py test --target ya`。
2. 更新 `todo.md` / `progress.md` / `feature_matrix.json`（验证结果、偏离、保留项）。
3. 不 commit；改动留工作区，主 agent 审查后提交。